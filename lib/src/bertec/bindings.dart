// FFI bindings for the two native libraries used by the app:
//  * BertecDevice.dll   - the vendor SDK, used directly by the custom path.
//  * bertec_bridge.dll  - the adapted example bridge, used by the example path.
//
// The struct layouts below mirror bertecif.h (#pragma pack(8)) and
// windows/bertec/bertec_bridge.h respectively. All packing here relies on
// natural alignment, which matches the C headers because every field's natural
// alignment is <= 8.

import 'dart:ffi';

import 'package:ffi/ffi.dart';

// ---------------------------------------------------------------------------
// bertecif.h structs (custom path)
// ---------------------------------------------------------------------------

const int kBertecMaxChannels = 32;

final class BChannelData extends Struct {
  @Int32()
  external int count;

  @Array(kBertecMaxChannels)
  external Array<Float> data;
}

final class BAdditionalData extends Struct {
  @Uint64()
  external int timestamp;

  @Uint64()
  external int eventCounter;

  @Uint64()
  external int frameCounter;

  @Uint8()
  external int auxData;

  @Uint8()
  external int syncData;
}

final class BDeviceData extends Struct {
  external BChannelData channelData;
  external BAdditionalData additionalData;
}

/// Mirror of bertec_DataStreamControl (six 4-byte fields => 24 bytes).
final class BDataStreamControl extends Struct {
  @Int32()
  external int size;

  @Int32()
  external int syncPinMode;

  @Int32()
  external int auxPinMode;

  @Int32()
  external int internalClockSource;

  @Float()
  external double internalClockFrequency;

  @Uint32()
  external int deviceFilterBitmask;
}

// bertec_DataStreamControl::SyncPinMode
const int kSyncPinModeNone = 0;
const int kSyncPinModeIntClock = 3;

// bertec_DataStreamControl::AuxPinMode
const int kAuxPinModeNone = 0;

// Selected bertec_StatusErrors values used by the UI.
const int kBertecNoError = 0;
const int kBertecNoDevicesFound = -5;
const int kBertecNoDataReceived = -11;
const int kBertecDeviceHasFaulted = -12;
const int kBertecLookingForDevices = -45;
const int kBertecInitializingDevices = -46;
const int kBertecDevicesReady = -50;
const int kBertecAutozeroWorking = -51;
const int kBertecAutozeroZeroFound = -52;
const int kBertecStreamSuccessful = -551;

/// The bertec_DataFrame header is `int deviceCount;` followed by an 8-byte
/// aligned flexible array, so device[0] begins at offset 8.
const int kDataFrameDeviceArrayOffset = 8;

// ---------------------------------------------------------------------------
// bertec_bridge.h structs (example path)
// ---------------------------------------------------------------------------

const int kBridgeMaxDevices = 8;
const int kBridgeMaxChannels = 32;

final class BridgeDevice extends Struct {
  @Uint64()
  external int timestamp;

  @Uint64()
  external int frameCounter;

  @Int32()
  external int syncData;

  @Int32()
  external int auxData;

  @Int32()
  external int channelCount;

  @Array(kBridgeMaxChannels)
  external Array<Float> channels;
}

final class BridgeFrame extends Struct {
  @Int32()
  external int deviceCount;

  @Array(kBridgeMaxDevices)
  external Array<BridgeDevice> devices;
}

// ---------------------------------------------------------------------------
// BertecDevice.dll function signatures
// ---------------------------------------------------------------------------

typedef _InitNative = Pointer<Void> Function();
typedef _HandleIntNative = Int32 Function(Pointer<Void>);
typedef HandleIntDart = int Function(Pointer<Void>);

typedef _SetEnableAutozeroNative = Int32 Function(Pointer<Void>, Int32);
typedef SetEnableAutozeroDart = int Function(Pointer<Void>, int);

typedef _ChannelCountNative = Int32 Function(Pointer<Void>, Int32);
typedef ChannelCountDart = int Function(Pointer<Void>, int);

typedef _ChannelNameNative = Int32 Function(
    Pointer<Void>, Int32, Int32, Pointer<Utf8>, Size);
typedef ChannelNameDart = int Function(
    Pointer<Void>, int, int, Pointer<Utf8>, int);

typedef _StreamControlNative = Int32 Function(
    Pointer<Void>, Pointer<BDataStreamControl>);
typedef StreamControlDart = int Function(
    Pointer<Void>, Pointer<BDataStreamControl>);

typedef _AllocateBufferNative = Pointer<Void> Function(
    Pointer<Void>, Pointer<Size>);
typedef AllocateBufferDart = Pointer<Void> Function(
    Pointer<Void>, Pointer<Size>);

typedef _ReadBufferedNative = Int32 Function(Pointer<Void>, Pointer<Void>, Size);
typedef ReadBufferedDart = int Function(Pointer<Void>, Pointer<Void>, int);

typedef _FreeBufferNative = Int32 Function(Pointer<Void>, Pointer<Void>);
typedef FreeBufferDart = int Function(Pointer<Void>, Pointer<Void>);

/// Wraps BertecDevice.dll for the custom FFI implementation.
class BertecLib {
  BertecLib(DynamicLibrary lib)
      : init = lib.lookupFunction<_InitNative, _InitNative>('bertec_Init'),
        close = lib
            .lookupFunction<_HandleIntNative, HandleIntDart>('bertec_Close'),
        start = lib
            .lookupFunction<_HandleIntNative, HandleIntDart>('bertec_Start'),
        stop = lib
            .lookupFunction<_HandleIntNative, HandleIntDart>('bertec_Stop'),
        getStatus = lib.lookupFunction<_HandleIntNative, HandleIntDart>(
            'bertec_GetStatus'),
        zeroNow = lib.lookupFunction<_HandleIntNative, HandleIntDart>(
            'bertec_ZeroNow'),
        getDeviceCount = lib.lookupFunction<_HandleIntNative, HandleIntDart>(
            'bertec_GetDeviceCount'),
        setEnableAutozero =
            lib.lookupFunction<_SetEnableAutozeroNative, SetEnableAutozeroDart>(
                'bertec_SetEnableAutozero'),
        getDeviceChannelCount =
            lib.lookupFunction<_ChannelCountNative, ChannelCountDart>(
                'bertec_GetDeviceChannelCount'),
        getDeviceChannelName =
            lib.lookupFunction<_ChannelNameNative, ChannelNameDart>(
                'bertec_GetDeviceChannelName'),
        canStartDataStream =
            lib.lookupFunction<_StreamControlNative, StreamControlDart>(
                'bertec_CanStartDataStream'),
        startDataStream =
            lib.lookupFunction<_StreamControlNative, StreamControlDart>(
                'bertec_StartDataStream'),
        getBufferedDataAvailable =
            lib.lookupFunction<_HandleIntNative, HandleIntDart>(
                'bertec_GetBufferedDataAvailable'),
        allocateReadBuffer =
            lib.lookupFunction<_AllocateBufferNative, AllocateBufferDart>(
                'bertec_AllocateReadBufferedData'),
        readBufferedDataStream =
            lib.lookupFunction<_ReadBufferedNative, ReadBufferedDart>(
                'bertec_ReadBufferedDataStream'),
        freeReadBuffer =
            lib.lookupFunction<_FreeBufferNative, FreeBufferDart>(
                'bertec_FreeAllocatedReadBufferedData');

  final Pointer<Void> Function() init;
  final HandleIntDart close;
  final HandleIntDart start;
  final HandleIntDart stop;
  final HandleIntDart getStatus;
  final HandleIntDart zeroNow;
  final HandleIntDart getDeviceCount;
  final SetEnableAutozeroDart setEnableAutozero;
  final ChannelCountDart getDeviceChannelCount;
  final ChannelNameDart getDeviceChannelName;
  final StreamControlDart canStartDataStream;
  final StreamControlDart startDataStream;
  final HandleIntDart getBufferedDataAvailable;
  final AllocateBufferDart allocateReadBuffer;
  final ReadBufferedDart readBufferedDataStream;
  final FreeBufferDart freeReadBuffer;

  static BertecLib open() => BertecLib(DynamicLibrary.open('BertecDevice.dll'));
}

// ---------------------------------------------------------------------------
// bertec_bridge.dll function signatures
// ---------------------------------------------------------------------------

typedef _VoidIntNative = Int32 Function();
typedef VoidIntDart = int Function();

typedef _IntIntNative = Int32 Function(Int32);
typedef IntIntDart = int Function(int);

typedef _ChannelNamesNative = Int32 Function(Int32, Pointer<Utf8>, Int32);
typedef ChannelNamesDart = int Function(int, Pointer<Utf8>, int);

typedef _PollNative = Int32 Function(Pointer<BridgeFrame>, Int32);
typedef PollDart = int Function(Pointer<BridgeFrame>, int);

/// Wraps bertec_bridge.dll for the example implementation.
class BridgeLib {
  BridgeLib(DynamicLibrary lib)
      : start =
            lib.lookupFunction<_IntIntNative, IntIntDart>('bridge_start'),
        stop = lib.lookupFunction<_VoidIntNative, VoidIntDart>('bridge_stop'),
        zeroNow =
            lib.lookupFunction<_VoidIntNative, VoidIntDart>('bridge_zero_now'),
        status =
            lib.lookupFunction<_VoidIntNative, VoidIntDart>('bridge_status'),
        isStreaming = lib
            .lookupFunction<_VoidIntNative, VoidIntDart>('bridge_is_streaming'),
        deviceCount = lib
            .lookupFunction<_VoidIntNative, VoidIntDart>('bridge_device_count'),
        fzIndex =
            lib.lookupFunction<_VoidIntNative, VoidIntDart>('bridge_fz_index'),
        channelNames =
            lib.lookupFunction<_ChannelNamesNative, ChannelNamesDart>(
                'bridge_channel_names'),
        poll = lib.lookupFunction<_PollNative, PollDart>('bridge_poll'),
        available = lib
            .lookupFunction<_VoidIntNative, VoidIntDart>('bridge_available');

  final IntIntDart start;
  final VoidIntDart stop;
  final VoidIntDart zeroNow;
  final VoidIntDart status;
  final VoidIntDart isStreaming;
  final VoidIntDart deviceCount;
  final VoidIntDart fzIndex;
  final ChannelNamesDart channelNames;
  final PollDart poll;
  final VoidIntDart available;

  static BridgeLib open() => BridgeLib(DynamicLibrary.open('bertec_bridge.dll'));
}
