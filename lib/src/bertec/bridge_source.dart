// Example implementation: drives bertec_bridge.dll, which reproduces the vendor
// example programs' SDK usage and exposes a pollable queue.

import 'dart:async';
import 'dart:ffi';

import 'package:ffi/ffi.dart';

import 'bertec_source.dart';
import 'bindings.dart';

class BridgeSource implements BertecSource {
  BridgeSource(this.variant);

  final ExampleVariant variant;

  static const Duration _pollInterval = Duration(milliseconds: 16);
  static const int _pollBatch = 512;

  final _frames = StreamController<ForceFrame>.broadcast();
  final _status = StreamController<BertecStatus>.broadcast();

  BridgeLib? _lib;
  Pointer<BridgeFrame> _batch = nullptr;
  Timer? _timer;
  bool _wasStreaming = false;
  BertecStatus _current = const BertecStatus.idle();

  @override
  SourceKind get kind => SourceKind.example;

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
    if (_lib != null) return;

    final lib = BridgeLib.open();
    _lib = lib;
    _batch = calloc<BridgeFrame>(_pollBatch);

    final rc = lib.start(variant.index);
    if (rc < 0 && rc != kBertecNoError) {
      _emitStatus(_current.copyWith(code: rc));
      throw StateError('bridge_start failed with code $rc '
          '(the FTDI/ftd2xx driver may be missing).');
    }

    _emitStatus(_current.copyWith(code: kBertecLookingForDevices));
    _timer = Timer.periodic(_pollInterval, (_) => _tick());
  }

  void _tick() {
    final lib = _lib;
    if (lib == null) return;

    final statusCode = lib.status();
    final streaming = lib.isStreaming() == 1;

    if (streaming && !_wasStreaming) {
      _wasStreaming = true;
      final channelNames = _readChannelNames(lib);
      _emitStatus(BertecStatus(
        code: statusCode,
        streaming: true,
        deviceCount: lib.deviceCount(),
        fzIndex: lib.fzIndex(),
        channelNames: channelNames,
      ));
    } else if (!streaming && _wasStreaming) {
      _wasStreaming = false;
      _emitStatus(_current.copyWith(code: statusCode, streaming: false));
    } else {
      _emitStatus(_current.copyWith(code: statusCode, streaming: streaming));
    }

    _drain(lib);
  }

  List<String> _readChannelNames(BridgeLib lib) {
    final buffer = calloc<Uint8>(512).cast<Utf8>();
    try {
      final count = lib.channelNames(0, buffer, 512);
      if (count <= 0) return const [];
      final joined = buffer.toDartString();
      if (joined.isEmpty) return const [];
      return joined.split(',');
    } finally {
      calloc.free(buffer);
    }
  }

  void _drain(BridgeLib lib) {
    if (_batch == nullptr) return;
    final frameSize = sizeOf<BridgeFrame>();
    var count = lib.poll(_batch, _pollBatch);
    while (count > 0) {
      for (var i = 0; i < count; i++) {
        final frame =
            Pointer<BridgeFrame>.fromAddress(_batch.address + i * frameSize).ref;
        final parsed = _parseFrame(frame);
        if (parsed != null && !_frames.isClosed) {
          _frames.add(parsed);
        }
      }
      if (count < _pollBatch) break;
      count = lib.poll(_batch, _pollBatch);
    }
  }

  ForceFrame? _parseFrame(BridgeFrame frame) {
    final deviceCount =
        frame.deviceCount > kBridgeMaxDevices ? kBridgeMaxDevices : frame.deviceCount;
    if (deviceCount <= 0) return null;

    final devices = <DeviceSample>[];
    for (var i = 0; i < deviceCount; i++) {
      final dev = frame.devices[i];
      final count = dev.channelCount > kBridgeMaxChannels
          ? kBridgeMaxChannels
          : dev.channelCount;
      if (count <= 0) continue;
      final channels = List<double>.generate(count, (c) => dev.channels[c]);
      devices.add(DeviceSample(
        timestamp: dev.timestamp,
        frameCounter: dev.frameCounter,
        syncData: dev.syncData,
        auxData: dev.auxData,
        channels: channels,
      ));
    }
    if (devices.isEmpty) return null;
    return ForceFrame(devices);
  }

  @override
  void zeroNow() {
    _lib?.zeroNow();
  }

  @override
  Future<void> stop() async {
    _timer?.cancel();
    _timer = null;
    final lib = _lib;
    if (lib != null) {
      lib.stop();
    }
    if (_batch != nullptr) {
      calloc.free(_batch);
      _batch = nullptr;
    }
    _lib = null;
    _wasStreaming = false;
    _emitStatus(const BertecStatus.idle());
  }

  @override
  void dispose() {
    stop();
    _frames.close();
    _status.close();
  }
}
