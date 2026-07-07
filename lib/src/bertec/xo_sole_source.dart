// Data source backed by the native xo_sole implementation in the Windows runner
// (windows/runner/xo_sole_windows_bertec.cpp). Unlike the FFI/bridge paths this
// talks to the native side over a MethodChannel (control) and an EventChannel
// (status + samples), and it records CSV natively rather than in Dart.

import 'dart:async';

import 'package:flutter/services.dart';

import 'bertec_source.dart';
import 'bindings.dart';

class XoSoleSource implements BertecSource {
  XoSoleSource();

  static const MethodChannel _method = MethodChannel('xo_sole/bertec');
  static const EventChannel _events =
      EventChannel('xo_sole/bertec_force_stream');

  // The native side emits fx/fy/fz per plate, so we present a synthetic
  // three-channel layout to the UI. FZ therefore lives at index 2.
  static const List<String> _channelNames = ['FX', 'FY', 'FZ'];
  static const int _fzIndex = 2;

  final StreamController<ForceFrame> _frames =
      StreamController<ForceFrame>.broadcast();
  final StreamController<BertecStatus> _statusCtrl =
      StreamController<BertecStatus>.broadcast();

  StreamSubscription<dynamic>? _eventSub;
  BertecStatus _current = const BertecStatus.idle();

  @override
  SourceKind get kind => SourceKind.xoSole;

  @override
  Stream<ForceFrame> get frames => _frames.stream;

  @override
  Stream<BertecStatus> get status => _statusCtrl.stream;

  @override
  BertecStatus get currentStatus => _current;

  @override
  Future<void> start() async {
    _eventSub = _events.receiveBroadcastStream().listen(
      _onEvent,
      onError: (Object error) {
        _emitStatus(_current.copyWith(
          code: kBertecNoDataReceived,
          streaming: false,
        ));
      },
    );
    await _method.invokeMethod<bool>('connect');
    _emitStatus(_current.copyWith(code: kBertecLookingForDevices));
  }

  @override
  Future<void> stop() async {
    try {
      await _method.invokeMethod<bool>('disconnect');
    } catch (_) {
      // Ignore disconnect failures during teardown.
    }
    await _eventSub?.cancel();
    _eventSub = null;
    _emitStatus(const BertecStatus.idle());
  }

  @override
  void zeroNow() {
    // Fire and forget; the native side runs the zero sequence and logs results.
    _method.invokeMethod<bool>('zero');
  }

  // --- Native CSV recording (see AppController) ----------------------------
  Future<bool> startNativeRecording({required String directory}) async {
    final ok = await _method.invokeMethod<bool>(
      'startRecording',
      <String, dynamic>{'outputDirectory': directory},
    );
    return ok ?? false;
  }

  Future<String?> stopNativeRecording() async {
    final result = await _method.invokeMethod<dynamic>('stopRecording');
    if (result is Map) {
      final path = result['filePath'];
      return path is String ? path : null;
    }
    return null;
  }

  // --- Event decoding ------------------------------------------------------
  void _onEvent(dynamic event) {
    if (event is! Map) return;
    final type = event['type'];
    if (type == 'status') {
      _onStatusEvent(event);
    } else if (type == 'sample') {
      _onSampleEvent(event);
    }
  }

  void _onStatusEvent(Map<dynamic, dynamic> event) {
    final phase = (event['phase'] as String?) ?? 'idle';
    final streaming = event['streaming'] == true;
    final deviceCount = _asInt(event['deviceCount']);
    _emitStatus(BertecStatus(
      code: _codeForPhase(phase, streaming),
      streaming: streaming,
      deviceCount: deviceCount,
      fzIndex: _fzIndex,
      channelNames: _channelNames,
    ));
  }

  void _onSampleEvent(Map<dynamic, dynamic> event) {
    final plates = event['plates'];
    if (plates is! List) return;
    final devices = <DeviceSample>[];
    for (final plate in plates) {
      if (plate is! Map) continue;
      final fx = _asDouble(plate['fx']);
      final fy = _asDouble(plate['fy']);
      final fz = _asDouble(plate['fz']);
      final timestamp =
          _composeTimestamp(plate['timestampMs'], plate['timestampMsHigh']);
      devices.add(DeviceSample(
        timestamp: timestamp,
        frameCounter: 0,
        syncData: 0,
        auxData: 0,
        channels: <double>[fx, fy, fz],
      ));
    }
    if (devices.isNotEmpty && !_frames.isClosed) {
      _frames.add(ForceFrame(devices));
    }
  }

  void _emitStatus(BertecStatus status) {
    _current = status;
    if (!_statusCtrl.isClosed) {
      _statusCtrl.add(status);
    }
  }

  int _codeForPhase(String phase, bool streaming) {
    if (streaming) return kBertecStreamSuccessful;
    switch (phase) {
      case 'searching':
        return kBertecLookingForDevices;
      case 'notFound':
        return kBertecNoDevicesFound;
      case 'streaming':
        return kBertecStreamSuccessful;
      case 'fault':
        return kBertecDeviceHasFaulted;
      default:
        return kBertecNoError;
    }
  }

  int _composeTimestamp(dynamic low, dynamic high) {
    final lowBits = _asInt(low) & 0xFFFFFFFF;
    final highBits = _asInt(high);
    return (highBits << 32) | lowBits;
  }

  int _asInt(dynamic value) {
    if (value is int) return value;
    if (value is num) return value.toInt();
    return 0;
  }

  double _asDouble(dynamic value) {
    if (value is double) return value;
    if (value is num) return value.toDouble();
    return 0.0;
  }

  @override
  void dispose() {
    _eventSub?.cancel();
    _frames.close();
    _statusCtrl.close();
  }
}
