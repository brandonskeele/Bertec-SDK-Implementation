// bertec_bridge.h
//
// A thin native bridge that reproduces the SDK-interaction logic of the Bertec
// vendor example programs, but routes each frame of data into a thread-safe
// queue that a Flutter/Dart caller drains via bridge_poll() instead of writing
// to a console or a file.
//
// The Dart FFI layer (lib/src/bertec/bridge_source.dart) mirrors the exported
// functions and the BridgeFrame layout defined below.

#ifndef BERTEC_BRIDGE_H
#define BERTEC_BRIDGE_H

#include <stdint.h>

#if defined(_WIN32)
#define BRIDGE_EXPORT __declspec(dllexport)
#else
#define BRIDGE_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Upper bounds used for the flattened marshaling struct. These are generous
// relative to a typical single/dual force-plate setup.
#define BRIDGE_MAX_DEVICES 8
#define BRIDGE_MAX_CHANNELS 32

// Which vendor example flow to reproduce. Values must stay in sync with the
// ExampleVariant enum on the Dart side.
enum BridgeVariant {
  BRIDGE_VARIANT_SIMPLE = 0,       // SimpleFZReaderExample.cpp
  BRIDGE_VARIANT_FULL = 1,         // BertecExample.cpp (all channels)
  BRIDGE_VARIANT_INTCLOCK = 2,     // InternalClockingFZReaderExample.cpp
  BRIDGE_VARIANT_AUX_STARTSTOP = 3,// AuxPinDataCollectionStartStopExample.cpp
  BRIDGE_VARIANT_MULTISYNC = 4,    // InternalClockingFZReaderExampleMultipleSync.cpp
};

// A flattened, fixed-size copy of a single device's data within a frame.
#pragma pack(push, 8)
struct BridgeDevice {
  uint64_t timestamp;
  uint64_t frameCounter;
  int32_t syncData;
  int32_t auxData;
  int32_t channelCount;
  float channels[BRIDGE_MAX_CHANNELS];
};

// A flattened copy of one bertec_DataFrame, safe to copy across the FFI
// boundary by value.
struct BridgeFrame {
  int32_t deviceCount;
  BridgeDevice devices[BRIDGE_MAX_DEVICES];
};
#pragma pack(pop)

// Starts device detection and streaming for the given BridgeVariant.
// Returns 0 on success, or a negative bertec_StatusErrors value on failure.
BRIDGE_EXPORT int bridge_start(int variant);

// Stops streaming, unregisters callbacks, and releases the SDK handle.
BRIDGE_EXPORT int bridge_stop(void);

// Zeroes the connected plate(s) against the current load (bertec_ZeroNow).
BRIDGE_EXPORT int bridge_zero_now(void);

// Returns the latest bertec status value (bertec_StatusErrors), or 0.
BRIDGE_EXPORT int bridge_status(void);

// Returns 1 once devices are ready and the data stream is running, else 0.
BRIDGE_EXPORT int bridge_is_streaming(void);

// Returns the number of connected devices, or 0.
BRIDGE_EXPORT int bridge_device_count(void);

// Returns the discovered FZ channel index for device 0, or -1.
BRIDGE_EXPORT int bridge_fz_index(void);

// Copies the comma-separated channel names for a device into buffer.
// Returns the channel count, or a negative error.
BRIDGE_EXPORT int bridge_channel_names(int deviceIndex, char* buffer, int bufferLen);

// Drains up to maxFrames queued frames into the caller-supplied array.
// Returns the number of frames written.
BRIDGE_EXPORT int bridge_poll(BridgeFrame* outFrames, int maxFrames);

// Returns the number of frames currently waiting in the queue.
BRIDGE_EXPORT int bridge_available(void);

#ifdef __cplusplus
}
#endif

#endif  // BERTEC_BRIDGE_H
