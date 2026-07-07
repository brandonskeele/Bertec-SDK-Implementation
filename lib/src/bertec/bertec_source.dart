// Shared models and the abstract data-source contract implemented by both the
// custom FFI path and the example bridge path.

import 'bindings.dart';

/// Which implementation drives data collection.
enum SourceKind {
  custom('Custom (Dart FFI)'),
  example('Example (vendor C++)'),
  xoSole('XO Sole (native channels)');

  const SourceKind(this.label);
  final String label;
}

/// The vendor example programs reproduced by the native bridge. The [index]
/// values must match BridgeVariant in windows/bertec/bertec_bridge.h.
enum ExampleVariant {
  simple('Simple FZ reader', 'SimpleFZReaderExample.cpp'),
  full('Full CSV recorder', 'BertecExample.cpp'),
  intClock('Internal clocking', 'InternalClockingFZReaderExample.cpp'),
  auxStartStop('Aux pin start/stop', 'AuxPinDataCollectionStartStopExample.cpp'),
  multiSync('Internal clock multi-sync',
      'InternalClockingFZReaderExampleMultipleSync.cpp');

  const ExampleVariant(this.label, this.sourceFile);
  final String label;
  final String sourceFile;
}

/// One device's slice of a frame.
class DeviceSample {
  DeviceSample({
    required this.timestamp,
    required this.frameCounter,
    required this.syncData,
    required this.auxData,
    required this.channels,
  });

  final int timestamp;
  final int frameCounter;
  final int syncData;
  final int auxData;
  final List<double> channels;
}

/// A single synchronized frame of data across all devices.
class ForceFrame {
  ForceFrame(this.devices);

  final List<DeviceSample> devices;

  int get deviceCount => devices.length;

  /// Convenience accessor for the FZ value of device 0.
  double? fz(int fzIndex) {
    if (devices.isEmpty || fzIndex < 0) return null;
    final channels = devices.first.channels;
    if (fzIndex >= channels.length) return null;
    return channels[fzIndex];
  }
}

/// A snapshot of the source's connection/streaming state for the UI.
class BertecStatus {
  const BertecStatus({
    required this.code,
    required this.streaming,
    required this.deviceCount,
    required this.fzIndex,
    required this.channelNames,
  });

  const BertecStatus.idle()
      : code = 0,
        streaming = false,
        deviceCount = 0,
        fzIndex = -1,
        channelNames = const [];

  final int code;
  final bool streaming;
  final int deviceCount;
  final int fzIndex;
  final List<String> channelNames;

  String get message => describeBertecStatus(code);

  BertecStatus copyWith({
    int? code,
    bool? streaming,
    int? deviceCount,
    int? fzIndex,
    List<String>? channelNames,
  }) {
    return BertecStatus(
      code: code ?? this.code,
      streaming: streaming ?? this.streaming,
      deviceCount: deviceCount ?? this.deviceCount,
      fzIndex: fzIndex ?? this.fzIndex,
      channelNames: channelNames ?? this.channelNames,
    );
  }
}

/// Maps a bertec_StatusErrors value to a human-readable message.
String describeBertecStatus(int code) {
  switch (code) {
    case kBertecNoError:
      return 'Idle';
    case kBertecLookingForDevices:
      return 'Searching for connected devices...';
    case kBertecInitializingDevices:
      return 'Initializing devices...';
    case kBertecNoDevicesFound:
      return 'No devices found - check USB connection and FTDI driver';
    case kBertecDevicesReady:
      return 'Devices ready';
    case kBertecNoDataReceived:
      return 'No data received - check cables';
    case kBertecDeviceHasFaulted:
      return 'Device faulted - power cycle the device';
    case kBertecAutozeroWorking:
      return 'Determining autozero...';
    case kBertecAutozeroZeroFound:
      return 'Autozero found';
    case kBertecStreamSuccessful:
      return 'Data stream running';
    default:
      return 'Status: $code';
  }
}

/// Contract for a Bertec data source. Both implementations poll the native side
/// on a timer and surface frames and status via streams.
abstract class BertecSource {
  SourceKind get kind;

  Stream<ForceFrame> get frames;
  Stream<BertecStatus> get status;

  /// Latest known status, useful for immediate UI rendering.
  BertecStatus get currentStatus;

  Future<void> start();
  Future<void> stop();

  /// Zeroes the plate(s) against the current load (bertec_ZeroNow).
  void zeroNow();

  void dispose();
}
