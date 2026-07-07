#include "bertec_sdk_impl.h"

#include <flutter/event_channel.h>
#include <flutter/event_sink.h>
#include <flutter/event_stream_handler_functions.h>
#include <flutter/method_channel.h>
#include <flutter/standard_method_codec.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <windows.h>
#include <shlobj.h>

#include "bertecif.h"

namespace {

using flutter::EncodableList;
using flutter::EncodableMap;
using flutter::EncodableValue;
using flutter::EventChannel;
using flutter::EventSink;
using flutter::MethodCall;
using flutter::MethodResult;
using flutter::StreamHandlerError;
using flutter::StreamHandlerFunctions;

constexpr int kMaxDevices = 8;
constexpr int kEmitIntervalMs = 16;

// Multiple amplifiers, self-syncing: device 0 drives SYNC at 1 kHz (Bertec INTCLOCK).
constexpr int kSyncMasterDeviceIndex = 0;
constexpr float kInternalClockFrequencyHz = 1000.0f;

// Spike diagnostics (option D): log when horizontal forces exceed this (N).
constexpr float kSpikeLogThresholdN = 1000.f;
constexpr ULONGLONG kSpikeLogCooldownMs = 500;

struct DeviceChannelMap {
  int fx = -1;
  int fy = -1;
  int fz = -1;
  int mx = -1;
  int my = -1;
  int mz = -1;
};

struct PlateChannelValues {
  float fx = 0.f;
  float fy = 0.f;
  float fz = 0.f;
  float mx = 0.f;
  float my = 0.f;
  float mz = 0.f;
};

struct DeviceChannelInfo {
  int count = 0;
  std::string names[BERTEC_MAX_CHANNELS];
};

struct PlateSample {
  int device_index = 0;
  float fx = 0.f;
  float fy = 0.f;
  float fz = 0.f;
  int64_t timestamp_ms = 0;
  bool valid = false;
};

void AppendTimestampFields(EncodableMap& map, int64_t timestamp_ms) {
  map[EncodableValue("timestampMs")] =
      EncodableValue(static_cast<int32_t>(timestamp_ms & 0x7FFFFFFF));
  map[EncodableValue("timestampMsHigh")] =
      EncodableValue(static_cast<int32_t>(timestamp_ms >> 32));
}

std::string PhaseFromStatus(int status) {
  switch (status) {
    case BERTEC_LOOKING_FOR_DEVICES:
    case BERTEC_INITIALIZING_DEVICES:
      return "searching";
    case BERTEC_NO_DEVICES_FOUND:
      return "notFound";
    case BERTEC_DEVICES_READY:
      return "streaming";
    case BERTEC_NO_DATA_RECEIVED:
    case BERTEC_DEVICE_HAS_FAULTED:
      return "fault";
    default:
      return "idle";
  }
}

bool IsAdvisoryStatus(int status) {
  switch (status) {
    case BERTEC_AUTOZEROSTATE_WORKING:
    case BERTEC_AUTOZEROSTATE_ZEROFOUND:
    case BERTEC_STREAM_STARTING:
      return true;
    default:
      return false;
  }
}

void LogBertecDebug(const char* message) {
  OutputDebugStringA("[app/bertec] ");
  OutputDebugStringA(message);
  OutputDebugStringA("\n");
}

void LogBertecDebugFormatted(const char* format, ...) {
  char buffer[2048] = {};
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  LogBertecDebug(buffer);
}

void LogBertecStatusError(int status, const char* context) {
  char message[256] = {};
  bertec_GetErrorString(static_cast<bertec_StatusErrors>(status), message,
                        sizeof(message));
  LogBertecDebugFormatted("%s failed: %d (%s)", context, status, message);
}

void LogLibraryVersion() {
  const unsigned int dll_version = bertec_LibraryVersion();
  LogBertecDebugFormatted(
      "Bertec DLL version 0x%X (header expects 0x%X)", dll_version,
      BERTEC_LIBRARY_VERSION);
  if (dll_version != BERTEC_LIBRARY_VERSION) {
    LogBertecDebug(
        "WARNING: Bertec DLL/header version mismatch — struct layout may differ");
  }
}

void LogDeviceInfo(bertec_Handle handle, int device_count) {
  for (int device_index = 0; device_index < device_count; ++device_index) {
    bertec_DeviceInfo info = {};
    const int result =
        bertec_GetDeviceInfo(handle, device_index, &info, sizeof(info));
    if (result != BERTEC_NOERROR) {
      LogBertecStatusError(result, "GetDeviceInfo");
      continue;
    }
    LogBertecDebugFormatted(
        "DeviceInfo[%d] serial=%s model=%s fw=0x%X status=%d "
        "sampleHz=%u hwid=0x%X channels=%d",
        device_index, info.serial, info.model, info.fwVersion, info.status,
        info.samplingFreq, info.hwid, info.channelCount);
  }
}

bertec_DataStreamControl BuildSelfSyncStreamControl() {
  bertec_DataStreamControl stream_control = {};
  stream_control.size = sizeof(stream_control);
  stream_control.syncPinMode =
      bertec_DataStreamControl::SyncPinMode::SYNCPINMODE_INTCLOCK;
  stream_control.auxPinMode =
      bertec_DataStreamControl::AuxPinMode::AUXPINMODE_NONE;
  stream_control.deviceFilterBitmask = 0;
  stream_control.internalClockSource = kSyncMasterDeviceIndex;
  stream_control.internalClockFrequency = kInternalClockFrequencyHz;
  return stream_control;
}

int FindChannelIndex(bertec_Handle handle, int device_index, const char* name) {
  const int channel_count = bertec_GetDeviceChannelCount(handle, device_index);
  char name_buffer[BERTEC_MAX_CHANNELNAME_LENGTH + 1] = {};
  for (int channel_index = 0; channel_index < channel_count; ++channel_index) {
    bertec_GetDeviceChannelName(handle, device_index, channel_index,
                                name_buffer, sizeof(name_buffer));
    if (_stricmp(name, name_buffer) == 0) {
      return channel_index;
    }
  }
  return -1;
}

void EnsureRawSdkStream(bertec_Handle handle) {
  if (handle == nullptr) {
    return;
  }
  // SDK defaults may retain prior settings between sessions; force raw stream.
  const int lowpass_result = bertec_SetLowpassFiltering(handle, 0);
  const int average_result = bertec_SetAveraging(handle, 0);
  LogBertecDebugFormatted(
      "SDK filtering disabled: lowpass off (err=%d), averaging off (err=%d)",
      lowpass_result, average_result);
}

void EnableAutozero(bertec_Handle handle) {
  if (handle == nullptr) {
    return;
  }
  const int result = bertec_SetEnableAutozero(handle, 0);
  if (result != BERTEC_NOERROR) {
    LogBertecStatusError(result, "SetEnableAutozero");
  }
}

bool ManualZeroPlates(bertec_Handle handle) {
  if (handle == nullptr) {
    return false;
  }

  if (bertec_ZeroNow(handle) != BERTEC_NOERROR) {
    LogBertecDebug("bertec_ZeroNow failed");
    return false;
  }

  constexpr int kMaxWaitIterations = 50;
  for (int iteration = 0; iteration < kMaxWaitIterations; ++iteration) {
    const bertec_AutozeroStates state = bertec_GetAutozeroState(handle);
    if (state == AUTOZEROSTATE_ZEROFOUND) {
      EnableAutozero(handle);
      LogBertecDebug("Manual zero complete");
      return true;
    }
    Sleep(100);
  }

  LogBertecDebug("Manual zero timed out waiting for AUTOZEROSTATE_ZEROFOUND");
  EnableAutozero(handle);
  return false;
}

std::string BuildRecordingFileName() {
  SYSTEMTIME local_time = {};
  GetLocalTime(&local_time);
  char buffer[64] = {};
  snprintf(buffer, sizeof(buffer), "bertec_force_%04u%02u%02u_%02u%02u%02u.csv",
           local_time.wYear, local_time.wMonth, local_time.wDay,
           local_time.wHour, local_time.wMinute, local_time.wSecond);
  return buffer;
}

std::string BuildDiagLogFileName() {
  SYSTEMTIME local_time = {};
  GetLocalTime(&local_time);
  char buffer[64] = {};
  snprintf(buffer, sizeof(buffer), "bertec_diag_%04u%02u%02u_%02u%02u%02u.csv",
           local_time.wYear, local_time.wMonth, local_time.wDay,
           local_time.wHour, local_time.wMinute, local_time.wSecond);
  return buffer;
}

std::string GetBertecRecordingsDirectory() {
  PWSTR documents_path = nullptr;
  if (FAILED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr,
                                  &documents_path)) ||
      documents_path == nullptr) {
    return {};
  }
  const std::filesystem::path directory =
      std::filesystem::path(documents_path) / "BertecRecordings";
  CoTaskMemFree(documents_path);
  return directory.string();
}

void AppendCsvField(std::ostream& out, const std::string& value) {
  bool needs_quotes = value.find_first_of(",\"\n\r") != std::string::npos;
  if (!needs_quotes) {
    out << value;
    return;
  }
  out << '"';
  for (char ch : value) {
    if (ch == '"') {
      out << "\"\"";
    } else {
      out << ch;
    }
  }
  out << '"';
}

struct DiagLogEntry {
  const char* event_type = "";
  int device_index = -1;
  const char* serial = "";
  const bertec_AdditionalData* additional = nullptr;
  float fx = 0.f;
  float fy = 0.f;
  float fz = 0.f;
  float prev_fx = 0.f;
  float prev_fy = 0.f;
  float prev_fz = 0.f;
  bool has_prev = false;
  float mx = 0.f;
  float my = 0.f;
  float mz = 0.f;
  int autozero_enable = -1;
  int autozero_state = -1;
  int status_code = 0;
  const char* detail = "";
};

PlateChannelValues ReadPlateChannels(const bertec_DeviceData& device_data,
                                     const DeviceChannelMap& channels) {
  PlateChannelValues values;
  if (channels.fx >= 0 && channels.fx < device_data.channelData.count) {
    values.fx = device_data.channelData.data[channels.fx];
  }
  if (channels.fy >= 0 && channels.fy < device_data.channelData.count) {
    values.fy = device_data.channelData.data[channels.fy];
  }
  if (channels.fz >= 0 && channels.fz < device_data.channelData.count) {
    values.fz = device_data.channelData.data[channels.fz];
  }
  if (channels.mx >= 0 && channels.mx < device_data.channelData.count) {
    values.mx = device_data.channelData.data[channels.mx];
  }
  if (channels.my >= 0 && channels.my < device_data.channelData.count) {
    values.my = device_data.channelData.data[channels.my];
  }
  if (channels.mz >= 0 && channels.mz < device_data.channelData.count) {
    values.mz = device_data.channelData.data[channels.mz];
  }
  return values;
}

PlateSample ReadPlateForces(int device_number,
                            const bertec_DeviceData& device_data,
                            const DeviceChannelMap& channels) {
  const PlateChannelValues values = ReadPlateChannels(device_data, channels);
  PlateSample sample;
  sample.device_index = device_number;
  sample.valid = true;
  sample.fx = values.fx;
  sample.fy = values.fy;
  sample.fz = values.fz;
  sample.timestamp_ms =
      static_cast<int64_t>(device_data.additionalData.timestamp);
  return sample;
}

void WriteBertecRecordingColumnHeader(std::ostream& out, int plate_count) {
  out << "Time (s),AUX,SYNC";
  for (int plate = 1; plate <= plate_count; ++plate) {
    out << ',' << plate << ":FX," << plate << ":FY," << plate << ":FZ,"
        << plate << ":MX," << plate << ":MY," << plate << ":MZ," << plate
        << ":COPX," << plate << ":COPY";
  }
  out << '\n';
}

bool PatchRecordingSampleCount(const std::filesystem::path& file_path,
                               uint64_t sample_count) {
  std::ifstream in(file_path, std::ios::binary);
  if (!in.is_open()) {
    return false;
  }

  std::vector<std::string> prefix_lines;
  std::string line;
  bool patched = false;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.rfind("N_OF_SAMPLES,", 0) == 0) {
      line = "N_OF_SAMPLES," + std::to_string(sample_count);
      patched = true;
    }
    prefix_lines.push_back(line);
    if (line == "DATA_STARTS") {
      break;
    }
  }
  if (!patched) {
    return false;
  }

  const std::vector<char> rest((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
  in.close();

  std::ofstream out(file_path, std::ios::binary | std::ios::trunc);
  if (!out.is_open()) {
    return false;
  }
  for (size_t index = 0; index < prefix_lines.size(); ++index) {
    if (index > 0) {
      out << '\n';
    }
    out << prefix_lines[index];
  }
  if (!rest.empty()) {
    out << '\n';
    out.write(rest.data(), static_cast<std::streamsize>(rest.size()));
  }
  return out.good();
}

class WindowsBertecHandler {
 public:
  explicit WindowsBertecHandler(flutter::BinaryMessenger* messenger)
      : method_channel_(std::make_unique<flutter::MethodChannel<EncodableValue>>(
            messenger,
            "app/bertec",
            &flutter::StandardMethodCodec::GetInstance())),
        event_channel_(std::make_unique<EventChannel<EncodableValue>>(
            messenger,
            "app/bertec_force_stream",
            &flutter::StandardMethodCodec::GetInstance())) {
    method_channel_->SetMethodCallHandler(
        [this](const auto& call, auto result) {
          HandleMethodCall(call, std::move(result));
        });

    auto handler = std::make_unique<StreamHandlerFunctions<EncodableValue>>(
        [this](const EncodableValue* arguments,
               std::unique_ptr<EventSink<EncodableValue>>&& events)
            -> std::unique_ptr<StreamHandlerError<EncodableValue>> {
          std::lock_guard<std::mutex> lock(stream_mutex_);
          event_sink_ = std::move(events);
          StartEmitThread();
          return nullptr;
        },
        [this](const EncodableValue* arguments)
            -> std::unique_ptr<StreamHandlerError<EncodableValue>> {
          std::lock_guard<std::mutex> lock(stream_mutex_);
          event_sink_.reset();
          StopEmitThread();
          return nullptr;
        });
    event_channel_->SetStreamHandler(std::move(handler));
  }

  ~WindowsBertecHandler() {
    DisconnectInternal();
    StopEmitThread();
  }

  void SetTargetWindow(HWND hwnd) {
    target_hwnd_ = hwnd;
    if (hwnd != nullptr) {
      PostMessage(hwnd, WM_BERTEC_DISPATCH, 0, 0);
    }
  }

  void FlushPendingEventsOnPlatformThread() {
    std::deque<EncodableValue> local;
    {
      std::lock_guard<std::mutex> lock(pending_mutex_);
      local.swap(pending_events_);
    }

    std::lock_guard<std::mutex> lock(stream_mutex_);
    if (!event_sink_) {
      return;
    }
    for (EncodableValue& event : local) {
      event_sink_->Success(std::move(event));
    }
  }

 private:
  static void CALLBACK StatusCallback(bertec_Handle handle, int status,
                                      void* user_data) {
    auto* self = static_cast<WindowsBertecHandler*>(user_data);
    if (self == nullptr) {
      return;
    }
    self->OnStatus(handle, status);
  }

  static void CALLBACK DataCallback(bertec_Handle handle,
                                    const bertec_DataFrame* data_frame,
                                    void* user_data) {
    auto* self = static_cast<WindowsBertecHandler*>(user_data);
    if (self == nullptr || data_frame == nullptr) {
      return;
    }
    self->OnDataFrame(handle, data_frame);
  }

  static void CALLBACK StreamStartCallback(bertec_Handle handle,
                                           const bertec_DataStreamControl*,
                                           int status, void* user_data) {
    auto* self = static_cast<WindowsBertecHandler*>(user_data);
    if (self == nullptr) {
      return;
    }
    self->OnStreamStart(handle, status);
  }

  void OnStatus(bertec_Handle handle, int status) {
    if (status == BERTEC_DATA_BUFFER_OVERFLOW) {
      LogBertecDebug(
          "BERTEC_DATA_BUFFER_OVERFLOW: SDK buffer saturated; samples may be "
          "dropped");
      DiagLogEntry entry;
      entry.event_type = "buffer_overflow";
      entry.status_code = status;
      AppendDiagLogEntry(entry);
    }

    if (status == BERTEC_DEVICE_HAS_FAULTED) {
      LogBertecDebug("BERTEC_DEVICE_HAS_FAULTED");
      DiagLogEntry entry;
      entry.event_type = "device_fault";
      entry.status_code = status;
      AppendDiagLogEntry(entry);
    }

    if (status == BERTEC_AUTOZEROSTATE_WORKING ||
        status == BERTEC_AUTOZEROSTATE_ZEROFOUND) {
      if (devices_ready_.load() && handle != nullptr) {
        LogBertecDebugFormatted(
            "Autozero status %d during streaming (enable=%d state=%d)",
            status, bertec_GetEnableAutozero(handle),
            static_cast<int>(bertec_GetAutozeroState(handle)));
        DiagLogEntry entry;
        entry.event_type = "autozero_status";
        entry.status_code = status;
        entry.autozero_enable = bertec_GetEnableAutozero(handle);
        entry.autozero_state =
            static_cast<int>(bertec_GetAutozeroState(handle));
        AppendDiagLogEntry(entry);
      }
      return;
    }

    if (IsAdvisoryStatus(status)) {
      return;
    }

    LogBertecDebugFormatted("Bertec status event: %d", status);
    DiagLogEntry entry;
    entry.event_type = "status";
    entry.status_code = status;
    if (handle != nullptr) {
      entry.autozero_enable = bertec_GetEnableAutozero(handle);
      entry.autozero_state =
          static_cast<int>(bertec_GetAutozeroState(handle));
    }
    AppendDiagLogEntry(entry);

    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      current_status_ = status;
      if (status == BERTEC_DEVICES_READY) {
        StartDataStreamAsync(handle);
        return;
      }
      if (status == BERTEC_NO_DATA_RECEIVED ||
          status == BERTEC_DEVICE_HAS_FAULTED ||
          status == BERTEC_NO_DEVICES_FOUND ||
          status == BERTEC_LOOKING_FOR_DEVICES) {
        devices_ready_ = false;
      }
    }
    EmitStatusEvent();
  }

  void StartDataStreamAsync(bertec_Handle handle) {
    const bertec_DataStreamControl stream_control = BuildSelfSyncStreamControl();

    const int preflight =
        bertec_CanStartDataStream(handle, &stream_control);
    if (preflight != BERTEC_NOERROR) {
      LogBertecStatusError(preflight,
                           "CanStartDataStream (INTCLOCK self-sync)");
      {
        std::lock_guard<std::mutex> lock(state_mutex_);
        devices_ready_ = false;
        current_status_ = BERTEC_STREAM_FAILURE;
      }
      EmitStatusEvent();
      return;
    }

    LogBertecDebugFormatted(
        "Starting INTCLOCK self-sync: master P%d @ %.0f Hz",
        kSyncMasterDeviceIndex + 1, kInternalClockFrequencyHz);

    bertec_StartDataStreamAsync(handle, &stream_control, StreamStartCallback,
                                this);
  }

  void OnStreamStart(bertec_Handle handle, int status) {
    if (status != BERTEC_STREAM_SUCCESSFUL) {
      LogBertecStatusError(status, "StartDataStreamAsync (INTCLOCK self-sync)");
      std::lock_guard<std::mutex> lock(state_mutex_);
      devices_ready_ = false;
      current_status_ = BERTEC_STREAM_FAILURE;
      EmitStatusEvent();
      return;
    }

    const int sync_reset = bertec_ResetSyncCounters(handle);
    if (sync_reset != BERTEC_NOERROR) {
      LogBertecStatusError(sync_reset, "ResetSyncCounters");
    } else {
      LogBertecDebug("Sync counters reset for self-synced plates");
    }

    const int unified = bertec_SetUnifiedDataMode(handle, 1);
    if (unified != BERTEC_NOERROR) {
      LogBertecStatusError(unified, "SetUnifiedDataMode");
    }

    const int reset_timestamps = bertec_ResetAllDeviceTimestamps(handle, 0);
    if (reset_timestamps != BERTEC_NOERROR) {
      LogBertecStatusError(reset_timestamps, "ResetAllDeviceTimestamps");
    } else {
      LogBertecDebug("Device timestamps reset to 0 for aligned graphing");
    }

    for (int i = 0; i < kMaxDevices; ++i) {
      has_last_sample_[i] = false;
    }

    StartDiagLog();
    LogLibraryVersion();
    DiscoverChannels(handle);
    LogDeviceInfo(handle, device_count_);
    LogStreamStartDiagnostics(handle);
    EnsureAutozeroEnabledLogged(handle, "stream start");
    LogDeviceChannelMaps(handle);

    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      devices_ready_ = true;
      current_status_ = BERTEC_DEVICES_READY;
    }
    EmitStatusEvent();
  }

  void DiscoverChannels(bertec_Handle handle) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    device_count_ = bertec_GetDeviceCount(handle);
    if (device_count_ > kMaxDevices) {
      device_count_ = kMaxDevices;
    }
    for (int i = 0; i < kMaxDevices; ++i) {
      device_channels_[i] = {};
      device_serials_[i].clear();
      device_channel_info_[i] = {};
    }
    for (int device_index = 0; device_index < device_count_; ++device_index) {
      device_channels_[device_index].fx =
          FindChannelIndex(handle, device_index, "FX");
      device_channels_[device_index].fy =
          FindChannelIndex(handle, device_index, "FY");
      device_channels_[device_index].fz =
          FindChannelIndex(handle, device_index, "FZ");
      device_channels_[device_index].mx =
          FindChannelIndex(handle, device_index, "MX");
      device_channels_[device_index].my =
          FindChannelIndex(handle, device_index, "MY");
      device_channels_[device_index].mz =
          FindChannelIndex(handle, device_index, "MZ");

      const int channel_count =
          bertec_GetDeviceChannelCount(handle, device_index);
      device_channel_info_[device_index].count = channel_count;
      for (int channel_index = 0; channel_index < channel_count; ++channel_index) {
        char name_buffer[BERTEC_MAX_CHANNELNAME_LENGTH + 1] = {};
        bertec_GetDeviceChannelName(handle, device_index, channel_index,
                                    name_buffer, sizeof(name_buffer));
        device_channel_info_[device_index].names[channel_index] = name_buffer;
      }

      char serial_buffer[BERTEC_MAX_SERIAL_LENGTH + 1] = {};
      bertec_GetDeviceSerialNumber(handle, device_index, serial_buffer,
                                   sizeof(serial_buffer));
      device_serials_[device_index] = serial_buffer;
    }
  }

  void LogDeviceChannelMaps(bertec_Handle handle) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    for (int device_index = 0; device_index < device_count_; ++device_index) {
      const int channel_count =
          bertec_GetDeviceChannelCount(handle, device_index);
      const auto& channels = device_channels_[device_index];
      LogBertecDebugFormatted(
          "Device %d serial=%s channels=%d FX@%d FY@%d FZ@%d",
          device_index, device_serials_[device_index].c_str(), channel_count,
          channels.fx, channels.fy, channels.fz);
      for (int channel_index = 0; channel_index < channel_count;
           ++channel_index) {
        char name_buffer[BERTEC_MAX_CHANNELNAME_LENGTH + 1] = {};
        bertec_GetDeviceChannelName(handle, device_index, channel_index,
                                    name_buffer, sizeof(name_buffer));
        LogBertecDebugFormatted("  [%d] %s", channel_index, name_buffer);
      }
    }
  }

  void AppendDiagLogEntry(const DiagLogEntry& entry) {
    std::lock_guard<std::mutex> lock(diag_mutex_);
    if (!diag_stream_.is_open()) {
      return;
    }

    SYSTEMTIME local_time = {};
    GetLocalTime(&local_time);
    char wall_time[40] = {};
    snprintf(wall_time, sizeof(wall_time), "%04u-%02u-%02uT%02u:%02u:%02u.%03u",
             local_time.wYear, local_time.wMonth, local_time.wDay,
             local_time.wHour, local_time.wMinute, local_time.wSecond,
             local_time.wMilliseconds);

    const uint64_t timestamp_ms =
        entry.additional != nullptr ? entry.additional->timestamp : 0;
    const uint64_t frame_counter =
        entry.additional != nullptr ? entry.additional->frameCounter : 0;
    const uint64_t event_counter =
        entry.additional != nullptr ? entry.additional->eventCounter : 0;
    const unsigned aux_data = entry.additional != nullptr
                                  ? entry.additional->auxData
                                  : 0;
    const unsigned sync_data = entry.additional != nullptr
                                   ? entry.additional->syncData
                                   : 0;

    const float dfx = entry.has_prev ? entry.fx - entry.prev_fx : 0.f;
    const float dfy = entry.has_prev ? entry.fy - entry.prev_fy : 0.f;
    const float dfz = entry.has_prev ? entry.fz - entry.prev_fz : 0.f;

    diag_stream_ << wall_time << ',' << entry.event_type << ','
                 << entry.device_index << ',';
    AppendCsvField(diag_stream_, entry.serial != nullptr ? entry.serial : "");
    diag_stream_ << ',' << timestamp_ms << ',' << frame_counter << ','
                 << event_counter << ',' << static_cast<unsigned>(aux_data)
                 << ',' << static_cast<unsigned>(sync_data) << ',' << entry.fx
                 << ',' << entry.fy << ',' << entry.fz << ',';
    if (entry.has_prev) {
      diag_stream_ << entry.prev_fx << ',' << entry.prev_fy << ','
                   << entry.prev_fz << ',';
    } else {
      diag_stream_ << ",,,";
    }
    diag_stream_ << dfx << ',' << dfy << ',' << dfz << ',' << entry.mx << ','
                 << entry.my << ',' << entry.mz << ','
                 << entry.autozero_enable << ',' << entry.autozero_state << ','
                 << entry.status_code << ',';
    AppendCsvField(diag_stream_,
                   entry.detail != nullptr ? entry.detail : "");
    diag_stream_ << '\n';
    diag_stream_.flush();
  }

  bool StartDiagLog() {
    StopDiagLog();

    const std::string output_directory = GetBertecRecordingsDirectory();
    if (output_directory.empty()) {
      LogBertecDebug("Failed to resolve Documents/BertecRecordings for diag log");
      return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(output_directory, ec);
    if (ec) {
      LogBertecDebugFormatted("Failed to create diag log directory: %s",
                              ec.message().c_str());
      return false;
    }

    const std::filesystem::path file_path =
        std::filesystem::path(output_directory) / BuildDiagLogFileName();

    std::lock_guard<std::mutex> lock(diag_mutex_);
    diag_stream_.open(file_path, std::ios::out | std::ios::trunc);
    if (!diag_stream_.is_open()) {
      LogBertecDebugFormatted("Failed to open diag log: %s",
                              file_path.string().c_str());
      return false;
    }

    diag_file_path_ = file_path.string();
    diag_stream_
        << "wall_time_local,event_type,device_index,serial,timestamp_ms,"
        << "frame_counter,event_counter,aux_data,sync_data,fx,fy,fz,prev_fx,"
        << "prev_fy,prev_fz,dfx,dfy,dfz,mx,my,mz,autozero_enable,"
        << "autozero_state,status_code,detail\n";
    diag_stream_.flush();
    LogBertecDebugFormatted("Diagnostic log started: %s",
                            diag_file_path_.c_str());
    return true;
  }

  void StopDiagLog() {
    std::lock_guard<std::mutex> lock(diag_mutex_);
    if (diag_stream_.is_open()) {
      diag_stream_.flush();
      diag_stream_.close();
      LogBertecDebugFormatted("Diagnostic log stopped: %s",
                              diag_file_path_.c_str());
    }
    diag_file_path_.clear();
  }

  void EnsureAutozeroEnabledLogged(bertec_Handle handle, const char* reason) {
    if (handle == nullptr) {
      return;
    }
    const int was_enabled = bertec_GetEnableAutozero(handle);
    EnableAutozero(handle);
    LogBertecDebugFormatted(
        "Autozero enabled (%s); enable=%d state=%d", reason,
        bertec_GetEnableAutozero(handle),
        static_cast<int>(bertec_GetAutozeroState(handle)));

    if (was_enabled != 0) {
      return;
    }

    DiagLogEntry entry;
    entry.event_type = "autozero_enabled";
    entry.autozero_enable = bertec_GetEnableAutozero(handle);
    entry.autozero_state =
        static_cast<int>(bertec_GetAutozeroState(handle));
    entry.detail = reason;
    AppendDiagLogEntry(entry);
  }

  void LogStreamStartDiagnostics(bertec_Handle handle) {
    DiagLogEntry start_entry;
    start_entry.event_type = "stream_start";
    char detail[128] = {};
    snprintf(detail, sizeof(detail), "sdk_version=0x%X header=0x%X",
             bertec_LibraryVersion(), BERTEC_LIBRARY_VERSION);
    start_entry.detail = detail;
    AppendDiagLogEntry(start_entry);

    std::lock_guard<std::mutex> lock(state_mutex_);
    for (int device_index = 0; device_index < device_count_; ++device_index) {
      bertec_DeviceInfo info = {};
      if (bertec_GetDeviceInfo(handle, device_index, &info, sizeof(info)) !=
          BERTEC_NOERROR) {
        continue;
      }

      char device_detail[256] = {};
      snprintf(device_detail, sizeof(device_detail),
               "model=%s fw=0x%X status=%d sampleHz=%u hwid=0x%X channels=%d",
               info.model, info.fwVersion, info.status, info.samplingFreq,
               info.hwid, info.channelCount);

      DiagLogEntry device_entry;
      device_entry.event_type = "device_info";
      device_entry.device_index = device_index;
      device_entry.serial = device_serials_[device_index].c_str();
      device_entry.status_code = info.status;
      device_entry.detail = device_detail;
      AppendDiagLogEntry(device_entry);
    }
  }

  void MaybeLogCalibrationTransition(
      bertec_Handle handle, int device_number,
      const bertec_DeviceData& device_data, const PlateSample& sample) {
    if (device_number < 0 || device_number >= kMaxDevices) {
      return;
    }

    const float last_fx = last_sample_fx_[device_number];
    const float last_fy = last_sample_fy_[device_number];
    const float last_fz = last_sample_fz_[device_number];
    const bool has_last = has_last_sample_[device_number];
    last_sample_fx_[device_number] = sample.fx;
    last_sample_fy_[device_number] = sample.fy;
    last_sample_fz_[device_number] = sample.fz;
    has_last_sample_[device_number] = true;
    if (!has_last) {
      return;
    }

    const float dfx = sample.fx - last_fx;
    const float dfy = sample.fy - last_fy;
    const float dfz = sample.fz - last_fz;
    constexpr float kTransitionThresholdN = 5000.f;
    if (std::fabs(dfx) < kTransitionThresholdN &&
        std::fabs(dfy) < kTransitionThresholdN &&
        std::fabs(dfz) < kTransitionThresholdN) {
      return;
    }

    const ULONGLONG now_ms = GetTickCount64();
    if (now_ms - last_transition_log_ms_[device_number] < kSpikeLogCooldownMs) {
      return;
    }
    last_transition_log_ms_[device_number] = now_ms;

    const auto& additional = device_data.additionalData;
    LogBertecDebugFormatted(
        "TRANSITION device=%d ts=%llu frame=%llu event=%llu aux=%u sync=%u "
        "dFX=%.1f dFY=%.1f dFZ=%.1f -> FX=%.1f FY=%.1f FZ=%.1f "
        "autozero(enable=%d state=%d)",
        device_number,
        static_cast<unsigned long long>(additional.timestamp),
        static_cast<unsigned long long>(additional.frameCounter),
        static_cast<unsigned long long>(additional.eventCounter),
        static_cast<unsigned>(additional.auxData),
        static_cast<unsigned>(additional.syncData), dfx, dfy, dfz, sample.fx,
        sample.fy, sample.fz, bertec_GetEnableAutozero(handle),
        static_cast<int>(bertec_GetAutozeroState(handle)));

    std::string serial;
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      if (device_number < device_count_) {
        serial = device_serials_[device_number];
      }
    }

    DiagLogEntry entry;
    entry.event_type = "transition";
    entry.device_index = device_number;
    entry.serial = serial.c_str();
    entry.additional = &additional;
    entry.fx = sample.fx;
    entry.fy = sample.fy;
    entry.fz = sample.fz;
    entry.prev_fx = last_fx;
    entry.prev_fy = last_fy;
    entry.prev_fz = last_fz;
    entry.has_prev = true;
    if (device_data.channelData.count > 5) {
      entry.mx = device_data.channelData.data[3];
      entry.my = device_data.channelData.data[4];
      entry.mz = device_data.channelData.data[5];
    }
    entry.autozero_enable = bertec_GetEnableAutozero(handle);
    entry.autozero_state =
        static_cast<int>(bertec_GetAutozeroState(handle));
    AppendDiagLogEntry(entry);
  }

  void MaybeLogForceSpike(bertec_Handle handle, int device_number,
                          const bertec_DeviceData& device_data,
                          const DeviceChannelMap& channels,
                          const PlateSample& sample) {
    if (std::fabs(sample.fx) < kSpikeLogThresholdN &&
        std::fabs(sample.fy) < kSpikeLogThresholdN) {
      return;
    }
    if (device_number < 0 || device_number >= kMaxDevices) {
      return;
    }

    const ULONGLONG now_ms = GetTickCount64();
    if (now_ms - last_spike_log_ms_[device_number] < kSpikeLogCooldownMs) {
      return;
    }
    last_spike_log_ms_[device_number] = now_ms;

    std::string serial;
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      if (device_number < device_count_) {
        serial = device_serials_[device_number];
      }
    }

    const auto& additional = device_data.additionalData;
    LogBertecDebugFormatted(
        "SPIKE device=%d serial=%s count=%d ts=%llu frame=%llu event=%llu "
        "mapped FX=%.3f FY=%.3f FZ=%.3f (idx %d/%d/%d)",
        device_number, serial.c_str(), device_data.channelData.count,
        static_cast<unsigned long long>(additional.timestamp),
        static_cast<unsigned long long>(additional.frameCounter),
        static_cast<unsigned long long>(additional.eventCounter), sample.fx,
        sample.fy, sample.fz, channels.fx, channels.fy, channels.fz);

    const int channel_count = device_data.channelData.count;
    for (int channel_index = 0; channel_index < channel_count; ++channel_index) {
      char name_buffer[BERTEC_MAX_CHANNELNAME_LENGTH + 1] = {};
      bertec_GetDeviceChannelName(handle, device_number, channel_index,
                                  name_buffer, sizeof(name_buffer));
      LogBertecDebugFormatted(
          "  channel[%d] %s = %.3f", channel_index, name_buffer,
          device_data.channelData.data[channel_index]);
    }

    float mx = 0.f;
    float my = 0.f;
    float mz = 0.f;
    if (channel_count > 5) {
      mx = device_data.channelData.data[3];
      my = device_data.channelData.data[4];
      mz = device_data.channelData.data[5];
    }

    char channel_detail[512] = {};
    int detail_len = 0;
    for (int channel_index = 0; channel_index < channel_count; ++channel_index) {
      char name_buffer[BERTEC_MAX_CHANNELNAME_LENGTH + 1] = {};
      bertec_GetDeviceChannelName(handle, device_number, channel_index,
                                  name_buffer, sizeof(name_buffer));
      if (detail_len > 0) {
        detail_len += snprintf(channel_detail + detail_len,
                               sizeof(channel_detail) - detail_len, ";");
      }
      detail_len += snprintf(
          channel_detail + detail_len, sizeof(channel_detail) - detail_len,
          "%s=%.3f", name_buffer,
          device_data.channelData.data[channel_index]);
      if (detail_len >= static_cast<int>(sizeof(channel_detail))) {
        break;
      }
    }

    DiagLogEntry entry;
    entry.event_type = "spike";
    entry.device_index = device_number;
    entry.serial = serial.c_str();
    entry.additional = &additional;
    entry.fx = sample.fx;
    entry.fy = sample.fy;
    entry.fz = sample.fz;
    entry.mx = mx;
    entry.my = my;
    entry.mz = mz;
    entry.autozero_enable = bertec_GetEnableAutozero(handle);
    entry.autozero_state =
        static_cast<int>(bertec_GetAutozeroState(handle));
    entry.detail = channel_detail;
    AppendDiagLogEntry(entry);
  }

  void OnDataFrame(bertec_Handle handle, const bertec_DataFrame* data_frame) {
    if (!devices_ready_.load()) {
      return;
    }

    int local_device_count = 0;
    DeviceChannelMap local_channels[kMaxDevices];
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      local_device_count = device_count_;
      for (int i = 0; i < local_device_count && i < kMaxDevices; ++i) {
        local_channels[i] = device_channels_[i];
      }
    }
    if (local_device_count <= 0) {
      return;
    }

    std::vector<PlateSample> plates;
    plates.reserve(local_device_count);

    for (int device_number = 0; device_number < local_device_count;
         ++device_number) {
      if (device_number >= data_frame->deviceCount) {
        return;
      }
      const bertec_DeviceData& device_data =
          data_frame->device[device_number];
      if (device_data.channelData.count <= 0) {
        return;
      }

      PlateSample sample = ReadPlateForces(device_number, device_data,
                                           local_channels[device_number]);
      MaybeLogCalibrationTransition(handle, device_number, device_data, sample);
      MaybeLogForceSpike(handle, device_number, device_data,
                         local_channels[device_number], sample);
      plates.push_back(sample);
    }

    if (!plates.empty()) {
      AppendRecordingFrame(data_frame);
    }

    if (plates.empty()) {
      return;
    }

    std::lock_guard<std::mutex> lock(sample_mutex_);
    latest_plates_ = std::move(plates);
    latest_timestamp_ms_ = static_cast<int64_t>(
        data_frame->device[0].additionalData.timestamp);
    has_sample_ = true;
  }

  void AppendRecordingFrame(const bertec_DataFrame* data_frame) {
    if (!recording_active_.load(std::memory_order_acquire)) {
      return;
    }
    if (data_frame == nullptr || data_frame->deviceCount <= 0) {
      return;
    }

    const bertec_AdditionalData& frame_additional =
        data_frame->device[0].additionalData;

    std::lock_guard<std::mutex> lock(recording_mutex_);
    if (!recording_stream_.is_open()) {
      return;
    }

    recording_stream_ << std::fixed << std::setprecision(3)
                      << (frame_additional.timestamp / 1000.0) << ','
                      << static_cast<unsigned>(frame_additional.auxData) << ','
                      << static_cast<unsigned>(frame_additional.syncData);

    for (int plate_index = 0; plate_index < recording_device_count_;
         ++plate_index) {
      PlateChannelValues values;
      if (plate_index < data_frame->deviceCount) {
        const bertec_DeviceData& device_data =
            data_frame->device[plate_index];
        if (device_data.channelData.count > 0) {
          DeviceChannelMap channels;
          {
            std::lock_guard<std::mutex> state_lock(state_mutex_);
            if (plate_index < device_count_) {
              channels = device_channels_[plate_index];
            }
          }
          values = ReadPlateChannels(device_data, channels);
        }
      }

      recording_stream_ << ',' << values.fx << ',' << values.fy << ','
                        << values.fz << ',' << values.mx << ',' << values.my
                        << ',' << values.mz << ",0,0";
    }

    recording_stream_ << '\n';
    recording_row_count_++;
    if (recording_row_count_ % 1000 == 0) {
      recording_stream_.flush();
    }
  }

  bool StartRecordingInternal(const std::string& output_directory,
                              const std::string& file_name = {}) {
    if (!devices_ready_.load()) {
      return false;
    }

    StopRecordingInternal();

    if (output_directory.empty()) {
      return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(output_directory, ec);
    if (ec) {
      LogBertecDebugFormatted("Failed to create recording directory: %s",
                              ec.message().c_str());
      return false;
    }

    std::string recording_file_name = BuildRecordingFileName();
    if (!file_name.empty()) {
      recording_file_name = file_name;
      if (recording_file_name.size() < 4 ||
          recording_file_name.substr(recording_file_name.size() - 4) != ".csv") {
        recording_file_name += ".csv";
      }
    }

    const std::filesystem::path file_path =
        std::filesystem::path(output_directory) / recording_file_name;

    int plate_count = 0;
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      plate_count = device_count_;
    }
    if (plate_count <= 0) {
      return false;
    }

    {
      std::lock_guard<std::mutex> lock(recording_mutex_);
      recording_stream_.open(file_path, std::ios::out | std::ios::trunc);
      if (!recording_stream_.is_open()) {
        LogBertecDebugFormatted("Failed to open recording file: %s",
                                file_path.string().c_str());
        return false;
      }

      recording_stream_ << "BERTEC_ForceData\n";
      recording_stream_ << "HEADER_LENGTH,1\n";
      recording_stream_ << "N_OF_SAMPLES,0\n";
      recording_stream_ << "DATA_STARTS\n";
      WriteBertecRecordingColumnHeader(recording_stream_, plate_count);
      recording_file_path_ = file_path.string();
      recording_row_count_ = 0;
      recording_device_count_ = plate_count;
    }

    recording_active_.store(true, std::memory_order_release);
    LogBertecDebugFormatted("Recording started: %s",
                            recording_file_path_.c_str());
    {
      DiagLogEntry entry;
      entry.event_type = "force_recording_started";
      entry.detail = recording_file_path_.c_str();
      AppendDiagLogEntry(entry);
    }
    return true;
  }

  EncodableMap StopRecordingInternal() {
    recording_active_.store(false, std::memory_order_release);

    EncodableMap result;
    std::filesystem::path file_path;
    uint64_t row_count = 0;
    {
      std::lock_guard<std::mutex> lock(recording_mutex_);
      if (recording_stream_.is_open()) {
        recording_stream_.flush();
        recording_stream_.close();
      }

      if (recording_file_path_.empty()) {
        return result;
      }

      file_path = std::filesystem::path(recording_file_path_);
      row_count = recording_row_count_;

      result[EncodableValue("filePath")] =
          EncodableValue(recording_file_path_);
      result[EncodableValue("rowCount")] =
          EncodableValue(static_cast<int32_t>(recording_row_count_));
      LogBertecDebugFormatted("Recording stopped: %s rows=%llu",
                              recording_file_path_.c_str(),
                              static_cast<unsigned long long>(recording_row_count_));

      recording_file_path_.clear();
      recording_row_count_ = 0;
      recording_device_count_ = 0;
    }

    if (!PatchRecordingSampleCount(file_path, row_count)) {
      LogBertecDebugFormatted(
          "Failed to patch N_OF_SAMPLES in recording file: %s",
          file_path.string().c_str());
    }

    {
      char detail[512] = {};
      snprintf(detail, sizeof(detail), "rows=%llu path=%s",
               static_cast<unsigned long long>(row_count),
               file_path.string().c_str());
      DiagLogEntry entry;
      entry.event_type = "force_recording_stopped";
      entry.detail = detail;
      AppendDiagLogEntry(entry);
    }

    return result;
  }

  void HandleMethodCall(const MethodCall<EncodableValue>& call,
                        std::unique_ptr<MethodResult<EncodableValue>> result) {
    const std::string& method = call.method_name();
    if (method == "connect") {
      const bool ok = ConnectInternal();
      if (ok) {
        result->Success(EncodableValue(true));
      } else {
        result->Error("connect_failed",
                      "Unable to initialize Bertec Device Library");
      }
      return;
    }
    if (method == "disconnect") {
      DisconnectInternal();
      result->Success(EncodableValue(true));
      return;
    }
    if (method == "getStatus") {
      result->Success(BuildStatusMap());
      return;
    }
    if (method == "zero") {
      const bool ok = ZeroNowInternal();
      if (ok) {
        result->Success(EncodableValue(true));
      } else {
        result->Error("zero_failed",
                      "Force plates are not connected or streaming.");
      }
      return;
    }
    if (method == "startRecording") {
      std::string output_directory;
      std::string file_name;
      const auto* args = std::get_if<EncodableMap>(call.arguments());
      if (args != nullptr) {
        const auto it =
            args->find(EncodableValue("outputDirectory"));
        if (it != args->end()) {
          if (const auto* directory =
                  std::get_if<std::string>(&it->second)) {
            output_directory = *directory;
          }
        }
        const auto file_it = args->find(EncodableValue("fileName"));
        if (file_it != args->end()) {
          if (const auto* name = std::get_if<std::string>(&file_it->second)) {
            file_name = *name;
          }
        }
      }
      const bool ok = StartRecordingInternal(output_directory, file_name);
      if (ok) {
        result->Success(EncodableValue(true));
      } else {
        result->Error("recording_start_failed",
                      "Unable to start Bertec force recording.");
      }
      return;
    }
    if (method == "stopRecording") {
      EncodableMap recording_result = StopRecordingInternal();
      if (recording_result.empty()) {
        result->Error("recording_stop_failed",
                      "No Bertec force recording was in progress.");
      } else {
        result->Success(EncodableValue(recording_result));
      }
      return;
    }
    result->NotImplemented();
  }

  bool ZeroNowInternal() {
    bertec_Handle handle = nullptr;
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      if (handle_ == nullptr || !devices_ready_.load()) {
        return false;
      }
      handle = handle_;
    }
    const bool ok = ManualZeroPlates(handle);
    DiagLogEntry entry;
    entry.event_type = "zero_manual";
    entry.detail = ok ? "complete" : "timeout";
    if (handle != nullptr) {
      entry.autozero_enable = bertec_GetEnableAutozero(handle);
      entry.autozero_state =
          static_cast<int>(bertec_GetAutozeroState(handle));
    }
    AppendDiagLogEntry(entry);
    return ok;
  }

  bool ConnectInternal() {
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      if (handle_ != nullptr) {
        return true;
      }

      handle_ = bertec_Init();
      if (handle_ == nullptr) {
        return false;
      }

      EnsureRawSdkStream(handle_);

      const int unified = bertec_SetUnifiedDataMode(handle_, 1);
      if (unified != BERTEC_NOERROR) {
        LogBertecStatusError(unified, "SetUnifiedDataMode (connect)");
      }

      devices_ready_ = false;
      current_status_ = BERTEC_LOOKING_FOR_DEVICES;
      device_count_ = 0;

      bertec_RegisterStatusCallback(handle_, StatusCallback, this);
      bertec_RegisterDataStreamCallback(handle_, DataCallback, this);
      bertec_Start(handle_);
    }
    EmitStatusEvent();
    return true;
  }

  void DisconnectInternal() {
    StopRecordingInternal();
    StopDiagLog();

    bertec_Handle handle = nullptr;
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      handle = handle_;
      handle_ = nullptr;
      devices_ready_ = false;
      device_count_ = 0;
      current_status_ = 0;
    }

    {
      std::lock_guard<std::mutex> lock(sample_mutex_);
      has_sample_ = false;
      latest_plates_.clear();
    }

    if (handle != nullptr) {
      bertec_UnregisterStatusCallback(handle, StatusCallback, this);
      bertec_UnregisterDataStreamCallback(handle, DataCallback, this);
      bertec_Stop(handle);
      bertec_Close(handle);
    }

    EmitStatusEvent();
  }

  EncodableMap BuildStatusMap() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    EncodableMap map;
    const bool streaming = devices_ready_.load();
    const std::string phase =
        streaming ? "streaming" : PhaseFromStatus(current_status_);
    map[EncodableValue("phase")] = EncodableValue(phase);
    map[EncodableValue("deviceCount")] = EncodableValue(device_count_);
    map[EncodableValue("streaming")] = EncodableValue(streaming);

    std::string diag_log_path;
    {
      std::lock_guard<std::mutex> diag_lock(diag_mutex_);
      diag_log_path = diag_file_path_;
    }
    if (!diag_log_path.empty()) {
      map[EncodableValue("diagLogPath")] = EncodableValue(diag_log_path);
    }

    EncodableList devices;
    for (int i = 0; i < device_count_; ++i) {
      EncodableMap device;
      device[EncodableValue("index")] = EncodableValue(i);
      device[EncodableValue("serial")] = EncodableValue(device_serials_[i]);
      const auto& channels = device_channels_[i];
      device[EncodableValue("fxIndex")] = EncodableValue(channels.fx);
      device[EncodableValue("fyIndex")] = EncodableValue(channels.fy);
      device[EncodableValue("fzIndex")] = EncodableValue(channels.fz);
      devices.push_back(EncodableValue(device));
    }
    map[EncodableValue("devices")] = EncodableValue(devices);
    return map;
  }

  void PostEventToPlatform(EncodableValue event) {
    {
      std::lock_guard<std::mutex> lock(pending_mutex_);
      pending_events_.push_back(std::move(event));
    }
    HWND hwnd = target_hwnd_;
    if (hwnd != nullptr) {
      PostMessage(hwnd, WM_BERTEC_DISPATCH, 0, 0);
    }
  }

  void EmitStatusEvent() {
    EncodableMap event;
    event[EncodableValue("type")] = EncodableValue("status");
    EncodableMap status = BuildStatusMap();
    for (const auto& entry : status) {
      event[entry.first] = entry.second;
    }
    PostEventToPlatform(EncodableValue(event));
  }

  void EmitSampleEvent() {
    std::vector<PlateSample> plates;
    int64_t timestamp_ms = 0;
    {
      std::lock_guard<std::mutex> lock(sample_mutex_);
      if (!has_sample_ || !devices_ready_.load()) {
        return;
      }
      plates = latest_plates_;
      timestamp_ms = latest_timestamp_ms_;
    }

    EncodableList plate_list;
    for (const PlateSample& plate : plates) {
      if (!plate.valid) {
        continue;
      }
      EncodableMap plate_map;
      plate_map[EncodableValue("deviceIndex")] =
          EncodableValue(plate.device_index);
      plate_map[EncodableValue("fx")] = EncodableValue(static_cast<double>(plate.fx));
      plate_map[EncodableValue("fy")] = EncodableValue(static_cast<double>(plate.fy));
      plate_map[EncodableValue("fz")] = EncodableValue(static_cast<double>(plate.fz));
      AppendTimestampFields(plate_map, plate.timestamp_ms);
      plate_list.push_back(EncodableValue(plate_map));
    }

    if (plate_list.empty()) {
      return;
    }

    EncodableMap event;
    event[EncodableValue("type")] = EncodableValue("sample");
    event[EncodableValue("timestampMs")] =
        EncodableValue(static_cast<int32_t>(timestamp_ms & 0x7FFFFFFF));
    event[EncodableValue("timestampMsHigh")] =
        EncodableValue(static_cast<int32_t>(timestamp_ms >> 32));
    event[EncodableValue("plates")] = EncodableValue(plate_list);

    PostEventToPlatform(EncodableValue(event));
  }

  void StartEmitThread() {
    if (emit_thread_running_.exchange(true)) {
      return;
    }
    emit_thread_ = std::thread([this]() {
      while (emit_thread_running_.load()) {
        EmitSampleEvent();
        std::this_thread::sleep_for(
            std::chrono::milliseconds(kEmitIntervalMs));
      }
    });
  }

  void StopEmitThread() {
    if (!emit_thread_running_.exchange(false)) {
      return;
    }
    if (emit_thread_.joinable()) {
      emit_thread_.join();
    }
  }

  std::unique_ptr<flutter::MethodChannel<EncodableValue>> method_channel_;
  std::unique_ptr<EventChannel<EncodableValue>> event_channel_;
  std::mutex stream_mutex_;
  std::unique_ptr<EventSink<EncodableValue>> event_sink_;

  std::mutex state_mutex_;
  bertec_Handle handle_ = nullptr;
  std::atomic<bool> devices_ready_{false};
  int current_status_ = 0;
  int device_count_ = 0;
  DeviceChannelMap device_channels_[kMaxDevices];
  DeviceChannelInfo device_channel_info_[kMaxDevices];
  std::string device_serials_[kMaxDevices];

  std::mutex sample_mutex_;
  std::vector<PlateSample> latest_plates_;
  int64_t latest_timestamp_ms_ = 0;
  bool has_sample_ = false;

  std::atomic<bool> emit_thread_running_{false};
  std::thread emit_thread_;

  HWND target_hwnd_ = nullptr;
  std::mutex pending_mutex_;
  std::deque<EncodableValue> pending_events_;

  ULONGLONG last_spike_log_ms_[kMaxDevices] = {};
  ULONGLONG last_transition_log_ms_[kMaxDevices] = {};
  float last_sample_fx_[kMaxDevices] = {};
  float last_sample_fy_[kMaxDevices] = {};
  float last_sample_fz_[kMaxDevices] = {};
  bool has_last_sample_[kMaxDevices] = {};

  std::atomic<bool> recording_active_{false};
  std::mutex recording_mutex_;
  std::ofstream recording_stream_;
  std::string recording_file_path_;
  uint64_t recording_row_count_ = 0;
  int recording_device_count_ = 0;

  std::mutex diag_mutex_;
  std::ofstream diag_stream_;
  std::string diag_file_path_;
};

WindowsBertecHandler* g_bertec_handler = nullptr;

}  // namespace

void WindowsBertecSetTargetWindow(HWND hwnd) {
  if (g_bertec_handler != nullptr) {
    g_bertec_handler->SetTargetWindow(hwnd);
  }
}

bool WindowsBertecHandleMessage(UINT message) {
  if (message != WM_BERTEC_DISPATCH || g_bertec_handler == nullptr) {
    return false;
  }
  g_bertec_handler->FlushPendingEventsOnPlatformThread();
  return true;
}

void WindowsBertecRegister(flutter::BinaryMessenger* messenger) {
  static std::unique_ptr<WindowsBertecHandler> handler;
  if (!handler) {
    handler = std::make_unique<WindowsBertecHandler>(messenger);
    g_bertec_handler = handler.get();
  }
}
