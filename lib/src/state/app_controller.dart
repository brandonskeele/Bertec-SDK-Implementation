// Orchestrates the selected data source, the live/CSV toggles, and the CSV
// recorder, exposing a single ChangeNotifier for the UI.

import 'dart:async';

import 'package:flutter/foundation.dart';
import 'package:path/path.dart' as p;
import 'package:path_provider/path_provider.dart';

import '../bertec/bertec_source.dart';
import '../bertec/bridge_source.dart';
import '../bertec/ffi_source.dart';
import '../bertec/xo_sole_source.dart';
import '../recording/csv_recorder.dart';

class AppController extends ChangeNotifier {
  static const Duration _liveThrottle = Duration(milliseconds: 33);

  SourceKind _sourceKind = SourceKind.xoSole;
  ExampleVariant _exampleVariant = ExampleVariant.simple;
  bool _showLive = true;
  bool _saveCsv = false;
  String? _outputFolder;

  bool _running = false;
  BertecStatus _status = const BertecStatus.idle();
  ForceFrame? _latestFrame;
  int _totalFrames = 0;
  String? _lastCsvPath;
  String? _errorMessage;
  DateTime? _lastZeroedAt;

  BertecSource? _source;
  bool _nativeRecording = false;
  final CsvRecorder _recorder = CsvRecorder();
  StreamSubscription<ForceFrame>? _frameSub;
  StreamSubscription<BertecStatus>? _statusSub;
  DateTime _lastLiveUpdate = DateTime.fromMillisecondsSinceEpoch(0);

  // --- Getters -------------------------------------------------------------
  SourceKind get sourceKind => _sourceKind;
  ExampleVariant get exampleVariant => _exampleVariant;
  bool get showLive => _showLive;
  bool get saveCsv => _saveCsv;
  String? get outputFolder => _outputFolder;
  bool get running => _running;
  BertecStatus get status => _status;
  ForceFrame? get latestFrame => _latestFrame;
  int get totalFrames => _totalFrames;
  bool get isRecording => _recorder.isRecording || _nativeRecording;
  int get recordedRows => _recorder.rowCount;

  /// True when CSV writing is handled by the native source, not the Dart
  /// recorder (so the row count is not tracked on the Dart side).
  bool get nativeRecording => _nativeRecording;
  String? get lastCsvPath => _lastCsvPath;
  String? get errorMessage => _errorMessage;
  DateTime? get lastZeroedAt => _lastZeroedAt;

  /// True once the source is actively streaming and can accept a zero request.
  bool get canZero => _running && _status.streaming;

  Future<void> init() async {
    try {
      final dir = await getApplicationDocumentsDirectory();
      _outputFolder = p.join(dir.path, 'BertecRecordings');
    } catch (_) {
      _outputFolder = null;
    }
    notifyListeners();
  }

  // --- Settings (locked while running) ------------------------------------
  void setSourceKind(SourceKind kind) {
    if (_running || kind == _sourceKind) return;
    _sourceKind = kind;
    notifyListeners();
  }

  void setExampleVariant(ExampleVariant variant) {
    if (_running || variant == _exampleVariant) return;
    _exampleVariant = variant;
    notifyListeners();
  }

  void setShowLive(bool value) {
    _showLive = value;
    notifyListeners();
  }

  void setSaveCsv(bool value) {
    _saveCsv = value;
    notifyListeners();
    unawaited(_syncRecording());
  }

  void setOutputFolder(String? folder) {
    _outputFolder = folder;
    notifyListeners();
  }

  // --- Lifecycle -----------------------------------------------------------
  Future<void> start() async {
    if (_running) return;
    _errorMessage = null;
    _totalFrames = 0;
    _latestFrame = null;
    _lastZeroedAt = null;

    final source = switch (_sourceKind) {
      SourceKind.custom => FfiSource(),
      SourceKind.example => BridgeSource(_exampleVariant),
      SourceKind.xoSole => XoSoleSource(),
    };
    _source = source;

    _frameSub = source.frames.listen(_onFrame);
    _statusSub = source.status.listen(_onStatus);

    try {
      await source.start();
      _running = true;
    } catch (e) {
      _errorMessage = e.toString();
      await _teardown();
    }
    notifyListeners();
  }

  Future<void> stop() async {
    if (!_running && _source == null) return;
    await _teardown();
    notifyListeners();
  }

  /// Manually zeroes the plate(s) against the current (unloaded) load.
  void zeroNow() {
    final source = _source;
    if (source == null || !_running) return;
    source.zeroNow();
    _lastZeroedAt = DateTime.now();
    notifyListeners();
  }

  Future<void> _teardown() async {
    await _recorder.stop();
    _nativeRecording = false;
    await _frameSub?.cancel();
    await _statusSub?.cancel();
    _frameSub = null;
    _statusSub = null;
    final source = _source;
    _source = null;
    if (source != null) {
      await source.stop();
      source.dispose();
    }
    _running = false;
    _status = const BertecStatus.idle();
  }

  // --- Data + status handlers ---------------------------------------------
  void _onFrame(ForceFrame frame) {
    _totalFrames++;
    if (_recorder.isRecording) {
      _recorder.add(frame);
    }
    if (_showLive) {
      final now = DateTime.now();
      if (now.difference(_lastLiveUpdate) >= _liveThrottle) {
        _lastLiveUpdate = now;
        _latestFrame = frame;
        notifyListeners();
      }
    }
  }

  void _onStatus(BertecStatus status) {
    _status = status;
    unawaited(_syncRecording());
    notifyListeners();
  }

  /// Opens or closes the CSV file to match the toggle and streaming state.
  Future<void> _syncRecording() async {
    final shouldRecord =
        _running && _saveCsv && _status.streaming && _outputFolder != null;

    // The xo_sole source records CSV natively; drive its start/stop instead of
    // running the Dart recorder.
    final source = _source;
    if (source is XoSoleSource) {
      if (shouldRecord && !_nativeRecording) {
        try {
          final ok = await source.startNativeRecording(directory: _outputFolder!);
          _nativeRecording = ok;
          if (ok) {
            _lastCsvPath = '$_outputFolder (native recorder)';
          } else {
            _errorMessage = 'Native recorder failed to start.';
          }
        } catch (e) {
          _errorMessage = 'Failed to start native recording: $e';
        }
        notifyListeners();
      } else if (!shouldRecord && _nativeRecording) {
        try {
          final path = await source.stopNativeRecording();
          if (path != null) _lastCsvPath = path;
        } catch (_) {
          // Ignore stop failures.
        }
        _nativeRecording = false;
        notifyListeners();
      }
      return;
    }

    if (shouldRecord && !_recorder.isRecording) {
      final path = _buildCsvPath();
      try {
        await _recorder.start(
          path,
          deviceCount: _status.deviceCount < 1 ? 1 : _status.deviceCount,
          channelNames: _status.channelNames,
        );
        _lastCsvPath = path;
        notifyListeners();
      } catch (e) {
        _errorMessage = 'Failed to open CSV: $e';
        notifyListeners();
      }
    } else if (!shouldRecord && _recorder.isRecording) {
      await _recorder.stop();
      notifyListeners();
    }
  }

  String _buildCsvPath() {
    final now = DateTime.now();
    String two(int v) => v.toString().padLeft(2, '0');
    final stamp = '${now.year}${two(now.month)}${two(now.day)}'
        '_${two(now.hour)}${two(now.minute)}${two(now.second)}';
    final kindLabel = _sourceKind == SourceKind.custom
        ? 'custom'
        : 'example_${_exampleVariant.name}';
    return p.join(_outputFolder!, 'bertec_${kindLabel}_$stamp.csv');
  }

  @override
  void dispose() {
    _teardown();
    super.dispose();
  }
}
