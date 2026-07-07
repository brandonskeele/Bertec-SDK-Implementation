// Custom implementation of the Bertec SDK: loads BertecDevice.dll directly and
// drives the full lifecycle from Dart using a polling state machine. This
// avoids delivering native callbacks into Dart from foreign threads.

import 'dart:async';
import 'dart:ffi';

import 'package:ffi/ffi.dart';

import 'bertec_source.dart';
import 'bindings.dart';

enum _Phase { idle, waitingReady, streaming }

class FfiSource implements BertecSource {
  FfiSource();

  static const Duration _pollInterval = Duration(milliseconds: 16);
  static const int _maxFramesPerTick = 4000;

  final _frames = StreamController<ForceFrame>.broadcast();
  final _status = StreamController<BertecStatus>.broadcast();

  BertecLib? _lib;
  Pointer<Void> _handle = nullptr;
  Pointer<Void> _readBuffer = nullptr;
  int _readBufferSize = 0;
  int _deviceDataSize = 0;

  Timer? _timer;
  _Phase _phase = _Phase.idle;
  BertecStatus _current = const BertecStatus.idle();

  @override
  SourceKind get kind => SourceKind.custom;

  @override
  Stream<ForceFrame> get frames => _frames.stream;

  @override
  Stream<BertecStatus> get status => _status.stream;

  @override
  BertecStatus get currentStatus => _current;

  void _emitStatus(BertecStatus status) {
    _current = status;
    if (!_status.isClosed) _status.add(status);
  }

  @override
  Future<void> start() async {
    if (_phase != _Phase.idle) return;

    final lib = BertecLib.open();
    _lib = lib;

    final handle = lib.init();
    if (handle == nullptr) {
      _emitStatus(_current.copyWith(code: -1));
      throw StateError(
          'bertec_Init failed - the FTDI (ftd2xx) driver may be missing.');
    }
    _handle = handle;
    _deviceDataSize = sizeOf<BDeviceData>();

    lib.start(handle);
    _phase = _Phase.waitingReady;
    _emitStatus(_current.copyWith(code: kBertecLookingForDevices));

    _timer = Timer.periodic(_pollInterval, (_) => _tick());
  }

  void _tick() {
    final lib = _lib;
    if (lib == null || _handle == nullptr) return;

    final statusCode = lib.getStatus(_handle);

    switch (_phase) {
      case _Phase.waitingReady:
        _emitStatus(_current.copyWith(code: statusCode));
        if (statusCode == kBertecDevicesReady) {
          _beginStreaming();
        }
        break;
      case _Phase.streaming:
        if (statusCode == kBertecNoDataReceived ||
            statusCode == kBertecDeviceHasFaulted ||
            statusCode == kBertecNoDevicesFound) {
          // Lost the devices; fall back to waiting for them to reappear.
          _phase = _Phase.waitingReady;
          _emitStatus(_current.copyWith(code: statusCode, streaming: false));
          return;
        }
        _drain();
        break;
      case _Phase.idle:
        break;
    }
  }

  void _beginStreaming() {
    final lib = _lib!;
    final control = calloc<BDataStreamControl>();
    try {
      control.ref
        ..size = sizeOf<BDataStreamControl>()
        ..syncPinMode = kSyncPinModeNone
        ..auxPinMode = kAuxPinModeNone
        ..internalClockSource = 0
        ..internalClockFrequency = 0
        ..deviceFilterBitmask = 0;

      if (lib.canStartDataStream(_handle, control) != kBertecNoError) {
        return; // try again next tick
      }
      final rc = lib.startDataStream(_handle, control);
      if (rc != kBertecNoError && rc != kBertecStreamSuccessful) {
        return; // not ready yet; retry next tick
      }
    } finally {
      calloc.free(control);
    }

    lib.setEnableAutozero(_handle, 1);

    // Allocate a read buffer sized for the currently connected devices.
    final sizeOut = calloc<Size>();
    try {
      _readBuffer = lib.allocateReadBuffer(_handle, sizeOut);
      _readBufferSize = sizeOut.value;
    } finally {
      calloc.free(sizeOut);
    }

    final channelNames = _readChannelNames();
    final fzIndex = _findFzIndex(channelNames);

    _phase = _Phase.streaming;
    _emitStatus(BertecStatus(
      code: kBertecStreamSuccessful,
      streaming: true,
      deviceCount: lib.getDeviceCount(_handle),
      fzIndex: fzIndex,
      channelNames: channelNames,
    ));
  }

  List<String> _readChannelNames() {
    final lib = _lib!;
    final channelCount = lib.getDeviceChannelCount(_handle, 0);
    if (channelCount <= 0) return const [];
    final names = <String>[];
    final buffer = calloc<Uint8>(64).cast<Utf8>();
    try {
      for (var c = 0; c < channelCount; c++) {
        lib.getDeviceChannelName(_handle, 0, c, buffer, 64);
        names.add(buffer.toDartString());
      }
    } finally {
      calloc.free(buffer);
    }
    return names;
  }

  int _findFzIndex(List<String> channelNames) {
    for (var i = 0; i < channelNames.length; i++) {
      if (channelNames[i].toUpperCase() == 'FZ') return i;
    }
    return -1;
  }

  void _drain() {
    final lib = _lib!;
    if (_readBuffer == nullptr) return;

    var pulled = 0;
    while (pulled < _maxFramesPerTick && lib.getBufferedDataAvailable(_handle) > 0) {
      final rc = lib.readBufferedDataStream(_handle, _readBuffer, _readBufferSize);
      if (rc <= 0) break;
      final frame = _parseFrame(_readBuffer);
      if (frame != null && !_frames.isClosed) {
        _frames.add(frame);
      }
      pulled++;
    }
  }

  ForceFrame? _parseFrame(Pointer<Void> base) {
    final deviceCount = base.cast<Int32>().value;
    if (deviceCount <= 0) return null;

    final devices = <DeviceSample>[];
    for (var i = 0; i < deviceCount; i++) {
      final address =
          base.address + kDataFrameDeviceArrayOffset + (i * _deviceDataSize);
      final dev = Pointer<BDeviceData>.fromAddress(address).ref;
      final count = dev.channelData.count;
      if (count <= 0) continue;

      final channels = List<double>.generate(
        count > kBertecMaxChannels ? kBertecMaxChannels : count,
        (c) => dev.channelData.data[c],
      );

      devices.add(DeviceSample(
        timestamp: dev.additionalData.timestamp,
        frameCounter: dev.additionalData.frameCounter,
        syncData: dev.additionalData.syncData,
        auxData: dev.additionalData.auxData,
        channels: channels,
      ));
    }
    if (devices.isEmpty) return null;
    return ForceFrame(devices);
  }

  @override
  void zeroNow() {
    final lib = _lib;
    if (lib != null && _handle != nullptr) {
      lib.zeroNow(_handle);
    }
  }

  @override
  Future<void> stop() async {
    _timer?.cancel();
    _timer = null;

    final lib = _lib;
    if (lib != null && _handle != nullptr) {
      if (_readBuffer != nullptr) {
        lib.freeReadBuffer(_handle, _readBuffer);
        _readBuffer = nullptr;
        _readBufferSize = 0;
      }
      lib.stop(_handle);
      lib.close(_handle);
    }
    _handle = nullptr;
    _phase = _Phase.idle;
    _emitStatus(const BertecStatus.idle());
  }

  @override
  void dispose() {
    stop();
    _frames.close();
    _status.close();
  }
}
