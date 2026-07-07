// bertec_bridge.cpp
//
// Adapted from the Bertec SDK vendor examples (SimpleFZReaderExample.cpp,
// BertecExample.cpp, InternalClockingFZReaderExample.cpp,
// AuxPinDataCollectionStartStopExample.cpp and
// InternalClockingFZReaderExampleMultipleSync.cpp).
//
// The SDK call sequence of each example is preserved: init -> register status
// and data callbacks -> start -> on BERTEC_DEVICES_READY build a per-variant
// bertec_DataStreamControl and call bertec_StartDataStreamAsync. The only
// change from the examples is that the data callback copies each frame into a
// thread-safe queue (instead of printf/file) so a Dart caller can drain it.

#include "bertec_bridge.h"

#include <windows.h>
#include <string.h>

#include <atomic>
#include <deque>
#include <mutex>

#include "bertecif.h"

#ifdef _WIN32
#define bridge_stricmp _stricmp
#else
#define bridge_stricmp strcasecmp
#endif

namespace {

// Keep roughly 20 seconds of single-plate 1000hz data before dropping the
// oldest frames. A Dart caller polling on a timer keeps the queue near empty.
constexpr size_t kMaxQueuedFrames = 20000;

bertec_Handle g_handle = nullptr;
std::atomic<int> g_variant{BRIDGE_VARIANT_SIMPLE};
std::atomic<int> g_status{0};
std::atomic<bool> g_devicesReady{false};
std::atomic<bool> g_streaming{false};
std::atomic<int> g_fzIndex{-1};
// Gates whether frames are enqueued. Always true except for the aux start/stop
// variant, where the physical AUX pin toggles it.
std::atomic<bool> g_recordingEnabled{true};

std::mutex g_queueMutex;
std::deque<BridgeFrame> g_queue;

// Forward declarations so registration can reference them.
void CALLBACK OnStatus(bertec_Handle bHand, int status, void* userData);
void CALLBACK OnData(bertec_Handle bHand, const bertec_DataFrame* dataFrame, void* userData);
void CALLBACK OnStreamStarted(bertec_Handle bHand, const bertec_DataStreamControl* control, int status, void* userData);
void CALLBACK OnAuxPinChanged(bertec_Handle bHand, bertec_IOPins pin, int deviceIndex, bool nowhigh, const bertec_DeviceData* data, void* userData);

void FindFzIndex() {
  int channelCount = bertec_GetDeviceChannelCount(g_handle, 0);
  int found = -1;
  char nameBuffer[16];
  for (int channelIndex = 0; channelIndex < channelCount; ++channelIndex) {
    bertec_GetDeviceChannelName(g_handle, 0, channelIndex, nameBuffer, sizeof(nameBuffer));
    if (bridge_stricmp("FZ", nameBuffer) == 0) {
      found = channelIndex;
      break;
    }
  }
  g_fzIndex.store(found);
}

void EnqueueFrame(const bertec_DataFrame* dataFrame) {
  BridgeFrame bf;
  memset(&bf, 0, sizeof(bf));

  int deviceCount = dataFrame->deviceCount;
  if (deviceCount > BRIDGE_MAX_DEVICES) deviceCount = BRIDGE_MAX_DEVICES;
  bf.deviceCount = deviceCount;

  for (int i = 0; i < deviceCount; ++i) {
    const bertec_DeviceData& src = dataFrame->device[i];
    BridgeDevice& dst = bf.devices[i];
    dst.timestamp = src.additionalData.timestamp;
    dst.frameCounter = src.additionalData.frameCounter;
    dst.syncData = src.additionalData.syncData;
    dst.auxData = src.additionalData.auxData;

    int channelCount = src.channelData.count;
    if (channelCount > BRIDGE_MAX_CHANNELS) channelCount = BRIDGE_MAX_CHANNELS;
    dst.channelCount = channelCount;
    for (int c = 0; c < channelCount; ++c) {
      dst.channels[c] = src.channelData.data[c];
    }
  }

  std::lock_guard<std::mutex> lock(g_queueMutex);
  g_queue.push_back(bf);
  while (g_queue.size() > kMaxQueuedFrames) {
    g_queue.pop_front();
  }
}

// Builds the per-variant control block, mirroring the setup used by each
// vendor example.
bertec_DataStreamControl BuildControl(int variant) {
  bertec_DataStreamControl control = {0};
  control.size = sizeof(control);
  control.syncPinMode = bertec_DataStreamControl::SyncPinMode::SYNCPINMODE_NONE;
  control.auxPinMode = bertec_DataStreamControl::AuxPinMode::AUXPINMODE_NONE;
  control.deviceFilterBitmask = 0;
  control.internalClockSource = 0;
  control.internalClockFrequency = 0;

  switch (variant) {
    case BRIDGE_VARIANT_INTCLOCK:
    case BRIDGE_VARIANT_MULTISYNC:
      // InternalClockingFZReaderExample(M): device 0 drives the clock at 1000hz.
      control.syncPinMode = bertec_DataStreamControl::SyncPinMode::SYNCPINMODE_INTCLOCK;
      control.internalClockSource = 0;
      control.internalClockFrequency = 1000;
      break;
    case BRIDGE_VARIANT_AUX_STARTSTOP:
    case BRIDGE_VARIANT_SIMPLE:
    case BRIDGE_VARIANT_FULL:
    default:
      // SimpleFZReaderExample / BertecExample / AuxPinDataCollectionStartStop:
      // classic non-synchronized stream. The aux variant additionally installs
      // a pin-state callback once the stream is running.
      break;
  }
  return control;
}

void CALLBACK OnStatus(bertec_Handle bHand, int status, void* userData) {
  (void)userData;
  g_status.store(status);

  switch ((bertec_StatusErrors)status) {
    case BERTEC_LOOKING_FOR_DEVICES:
    case BERTEC_NO_DEVICES_FOUND:
      g_devicesReady.store(false);
      g_streaming.store(false);
      g_fzIndex.store(-1);
      break;

    case BERTEC_DEVICES_READY: {
      // Kick off the data stream on a worker thread (required since v2.56 the
      // SDK forbids starting the stream from inside a callback).
      bertec_DataStreamControl control = BuildControl(g_variant.load());
      bertec_StartDataStreamAsync(bHand, &control, OnStreamStarted, nullptr);
      break;
    }

    case BERTEC_NO_DATA_RECEIVED:
    case BERTEC_DEVICE_HAS_FAULTED:
      g_devicesReady.store(false);
      g_streaming.store(false);
      g_fzIndex.store(-1);
      break;

    default:
      break;
  }
}

void CALLBACK OnStreamStarted(bertec_Handle bHand, const bertec_DataStreamControl* control, int status, void* userData) {
  (void)control;
  (void)userData;
  if (status == BERTEC_STREAM_SUCCESSFUL) {
    FindFzIndex();
    bertec_SetEnableAutozero(bHand, 1);

    if (g_variant.load() == BRIDGE_VARIANT_AUX_STARTSTOP) {
      // The AUX pin drives recording start/stop; wait for the first pulse.
      g_recordingEnabled.store(false);
      bertec_RegisterPinStateChangeCallback(bHand, IO_PIN_AUX, OnAuxPinChanged, nullptr);
    } else {
      g_recordingEnabled.store(true);
    }

    g_devicesReady.store(true);
    g_streaming.store(true);
  }
}

void CALLBACK OnData(bertec_Handle bHand, const bertec_DataFrame* dataFrame, void* userData) {
  (void)bHand;
  (void)userData;
  if (!g_devicesReady.load() || !g_recordingEnabled.load()) return;
  if (dataFrame == nullptr || dataFrame->deviceCount <= 0) return;
  EnqueueFrame(dataFrame);
}

void CALLBACK OnAuxPinChanged(bertec_Handle bHand, bertec_IOPins pin, int deviceIndex, bool nowhigh, const bertec_DeviceData* data, void* userData) {
  (void)bHand;
  (void)data;
  (void)userData;
  if (pin == IO_PIN_AUX && deviceIndex == 0) {
    // High level starts recording, low level stops (the "Hilo" behavior).
    g_recordingEnabled.store(nowhigh);
  }
}

}  // namespace

extern "C" {

BRIDGE_EXPORT int bridge_start(int variant) {
  if (g_handle != nullptr) {
    bridge_stop();
  }

  g_variant.store(variant);
  g_devicesReady.store(false);
  g_streaming.store(false);
  g_fzIndex.store(-1);
  g_recordingEnabled.store(variant != BRIDGE_VARIANT_AUX_STARTSTOP);
  g_status.store(0);
  {
    std::lock_guard<std::mutex> lock(g_queueMutex);
    g_queue.clear();
  }

  g_handle = bertec_Init();
  if (g_handle == nullptr) {
    return BERTEC_GENERIC_ERROR;
  }

  bertec_RegisterStatusCallback(g_handle, OnStatus, nullptr);
  bertec_RegisterDataStreamCallback(g_handle, OnData, nullptr);

  return bertec_Start(g_handle);
}

BRIDGE_EXPORT int bridge_stop(void) {
  if (g_handle == nullptr) {
    return BERTEC_NOERROR;
  }

  bertec_UnregisterStatusCallback(g_handle, OnStatus, nullptr);
  bertec_UnregisterDataStreamCallback(g_handle, OnData, nullptr);
  bertec_UnregisterPinStateChangeCallback(g_handle, IO_PIN_AUX, OnAuxPinChanged, nullptr);
  bertec_Stop(g_handle);
  bertec_Close(g_handle);
  g_handle = nullptr;

  g_devicesReady.store(false);
  g_streaming.store(false);
  g_fzIndex.store(-1);
  {
    std::lock_guard<std::mutex> lock(g_queueMutex);
    g_queue.clear();
  }
  return BERTEC_NOERROR;
}

BRIDGE_EXPORT int bridge_zero_now(void) {
  if (g_handle == nullptr) return BERTEC_ERROR_INVALIDHANDLE;
  return bertec_ZeroNow(g_handle);
}

BRIDGE_EXPORT int bridge_status(void) {
  return g_status.load();
}

BRIDGE_EXPORT int bridge_is_streaming(void) {
  return g_streaming.load() ? 1 : 0;
}

BRIDGE_EXPORT int bridge_device_count(void) {
  if (g_handle == nullptr) return 0;
  return bertec_GetDeviceCount(g_handle);
}

BRIDGE_EXPORT int bridge_fz_index(void) {
  return g_fzIndex.load();
}

BRIDGE_EXPORT int bridge_channel_names(int deviceIndex, char* buffer, int bufferLen) {
  if (buffer == nullptr || bufferLen <= 0) return BERTEC_INVALID_PARAMETER;
  buffer[0] = '\0';
  if (g_handle == nullptr) return 0;

  int channelCount = bertec_GetDeviceChannelCount(g_handle, deviceIndex);
  int written = 0;
  char nameBuffer[BERTEC_MAX_CHANNELNAME_LENGTH + 1];
  for (int c = 0; c < channelCount; ++c) {
    nameBuffer[0] = '\0';
    bertec_GetDeviceChannelName(g_handle, deviceIndex, c, nameBuffer, sizeof(nameBuffer));
    int nameLen = (int)strlen(nameBuffer);
    int need = nameLen + (c > 0 ? 1 : 0);
    if (written + need >= bufferLen) break;
    if (c > 0) buffer[written++] = ',';
    memcpy(buffer + written, nameBuffer, (size_t)nameLen + 1);
    written += nameLen;
  }
  return channelCount;
}

BRIDGE_EXPORT int bridge_poll(BridgeFrame* outFrames, int maxFrames) {
  if (outFrames == nullptr || maxFrames <= 0) return 0;
  std::lock_guard<std::mutex> lock(g_queueMutex);
  int count = 0;
  while (count < maxFrames && !g_queue.empty()) {
    outFrames[count] = g_queue.front();
    g_queue.pop_front();
    ++count;
  }
  return count;
}

BRIDGE_EXPORT int bridge_available(void) {
  std::lock_guard<std::mutex> lock(g_queueMutex);
  return (int)g_queue.size();
}

}  // extern "C"
