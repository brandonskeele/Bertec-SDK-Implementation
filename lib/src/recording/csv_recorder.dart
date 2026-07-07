// Streams recorded frames to a CSV file. Only active while a recording is open;
// the caller decides when to start/stop based on the "Save CSV" toggle.

import 'dart:io';

import '../bertec/bertec_source.dart';

class CsvRecorder {
  IOSink? _sink;
  String? _path;
  int _rowCount = 0;

  bool get isRecording => _sink != null;
  String? get path => _path;
  int get rowCount => _rowCount;

  Future<void> start(
    String filePath, {
    required int deviceCount,
    required List<String> channelNames,
  }) async {
    await stop();
    final file = File(filePath);
    await file.parent.create(recursive: true);
    final sink = file.openWrite();
    sink.writeln(_buildHeader(deviceCount, channelNames));
    _sink = sink;
    _path = filePath;
    _rowCount = 0;
  }

  String _buildHeader(int deviceCount, List<String> channelNames) {
    final columns = <String>[];
    final multi = deviceCount > 1;
    for (var d = 0; d < (deviceCount < 1 ? 1 : deviceCount); d++) {
      final suffix = multi ? '-${d + 1}' : '';
      columns.add('Timestamp$suffix');
      columns.add('FrameCounter$suffix');
      final count = channelNames.isEmpty ? 0 : channelNames.length;
      if (count == 0) {
        // Unknown channel count until first frame; label generically.
        columns.add('Channels$suffix');
      } else {
        for (var c = 0; c < count; c++) {
          columns.add('${channelNames[c]}$suffix');
        }
      }
    }
    return columns.join(',');
  }

  void add(ForceFrame frame) {
    final sink = _sink;
    if (sink == null) return;

    final cells = <String>[];
    for (final device in frame.devices) {
      cells.add(device.timestamp.toString());
      cells.add(device.frameCounter.toString());
      for (final value in device.channels) {
        cells.add(value.toStringAsFixed(6));
      }
    }
    sink.write(cells.join(','));
    sink.write('\n');
    _rowCount++;
  }

  Future<void> stop() async {
    final sink = _sink;
    _sink = null;
    if (sink != null) {
      await sink.flush();
      await sink.close();
    }
  }
}
