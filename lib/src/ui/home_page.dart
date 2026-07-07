import 'package:file_selector/file_selector.dart';
import 'package:flutter/material.dart';

import '../bertec/bertec_source.dart';
import '../state/app_controller.dart';

/// Set to true to expose the Custom vs XO Sole data-source selector in the UI.
/// Left false for the shipping build, which uses only the XO Sole native source
/// (see AppController's default SourceKind). Both paths remain wired in code.
const bool kShowDataSourceSelector = false;

class HomePage extends StatelessWidget {
  const HomePage({super.key, required this.controller});

  final AppController controller;

  @override
  Widget build(BuildContext context) {
    return ListenableBuilder(
      listenable: controller,
      builder: (context, _) {
        return Scaffold(
          appBar: AppBar(
            title: const Text('Bertec Force Plate Recorder'),
            backgroundColor: Theme.of(context).colorScheme.inversePrimary,
          ),
          body: Center(
            child: ConstrainedBox(
              constraints: const BoxConstraints(maxWidth: 900),
              child: ListView(
                padding: const EdgeInsets.all(20),
                children: [
                  if (kShowDataSourceSelector) ...[
                    _SourceCard(controller: controller),
                    const SizedBox(height: 16),
                  ],
                  _OptionsCard(controller: controller),
                  const SizedBox(height: 16),
                  _ControlCard(controller: controller),
                  const SizedBox(height: 16),
                  _StatusCard(controller: controller),
                  const SizedBox(height: 16),
                  _LiveCard(controller: controller),
                ],
              ),
            ),
          ),
        );
      },
    );
  }
}

class _SectionCard extends StatelessWidget {
  const _SectionCard({required this.title, required this.child, this.icon});

  final String title;
  final Widget child;
  final IconData? icon;

  @override
  Widget build(BuildContext context) {
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              children: [
                if (icon != null) ...[
                  Icon(icon, size: 20, color: Theme.of(context).colorScheme.primary),
                  const SizedBox(width: 8),
                ],
                Text(title, style: Theme.of(context).textTheme.titleMedium),
              ],
            ),
            const SizedBox(height: 12),
            child,
          ],
        ),
      ),
    );
  }
}

class _SourceCard extends StatelessWidget {
  const _SourceCard({required this.controller});
  final AppController controller;

  // Implementations exposed for testing. The example (vendor C++) bridge is
  // still available in code but intentionally omitted from this selector.
  static const List<SourceKind> _options = [
    SourceKind.custom,
    SourceKind.xoSole,
  ];

  @override
  Widget build(BuildContext context) {
    final running = controller.running;
    return _SectionCard(
      title: 'Data source',
      icon: Icons.cable,
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          SegmentedButton<SourceKind>(
            segments: [
              for (final kind in _options)
                ButtonSegment<SourceKind>(
                  value: kind,
                  label: Text(kind.label),
                ),
            ],
            selected: {controller.sourceKind},
            onSelectionChanged: running
                ? null
                : (selection) => controller.setSourceKind(selection.first),
          ),
          const SizedBox(height: 8),
          Text(
            running
                ? 'Stop to switch implementations.'
                : 'XO Sole uses the native runner integration '
                    '(records CSV + diagnostics to Documents/BertecRecordings).',
            style: const TextStyle(fontSize: 12, color: Colors.grey),
          ),
        ],
      ),
    );
  }
}

class _OptionsCard extends StatelessWidget {
  const _OptionsCard({required this.controller});
  final AppController controller;

  Future<void> _pickFolder() async {
    final folder = await getDirectoryPath();
    if (folder != null) controller.setOutputFolder(folder);
  }

  @override
  Widget build(BuildContext context) {
    return _SectionCard(
      title: 'Recording options',
      icon: Icons.tune,
      child: Column(
        children: [
          SwitchListTile(
            contentPadding: EdgeInsets.zero,
            title: const Text('Show live readout'),
            subtitle: const Text('Display FZ and channel values in real time'),
            value: controller.showLive,
            onChanged: controller.setShowLive,
          ),
          SwitchListTile(
            contentPadding: EdgeInsets.zero,
            title: const Text('Save CSV'),
            subtitle: const Text('Write every frame to a CSV file while streaming'),
            value: controller.saveCsv,
            onChanged: controller.setSaveCsv,
          ),
          const Divider(),
          Row(
            children: [
              const Icon(Icons.folder_outlined, size: 20),
              const SizedBox(width: 8),
              Expanded(
                child: Text(
                  controller.outputFolder ?? 'No output folder selected',
                  overflow: TextOverflow.ellipsis,
                  style: const TextStyle(fontSize: 13),
                ),
              ),
              TextButton(onPressed: _pickFolder, child: const Text('Change')),
            ],
          ),
        ],
      ),
    );
  }
}

class _ControlCard extends StatelessWidget {
  const _ControlCard({required this.controller});
  final AppController controller;

  @override
  Widget build(BuildContext context) {
    final running = controller.running;
    return _SectionCard(
      title: 'Control',
      icon: Icons.play_circle_outline,
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Expanded(
                child: FilledButton.icon(
                  onPressed: running ? controller.stop : controller.start,
                  icon: Icon(running ? Icons.stop : Icons.play_arrow),
                  label: Text(running ? 'Stop' : 'Start'),
                  style: FilledButton.styleFrom(
                    backgroundColor: running ? Colors.red : null,
                    padding: const EdgeInsets.symmetric(vertical: 16),
                  ),
                ),
              ),
              const SizedBox(width: 12),
              Expanded(
                child: OutlinedButton.icon(
                  onPressed: controller.canZero
                      ? () {
                          controller.zeroNow();
                          ScaffoldMessenger.of(context)
                            ..hideCurrentSnackBar()
                            ..showSnackBar(const SnackBar(
                              content: Text('Zeroing plates - keep them unloaded'),
                              duration: Duration(seconds: 2),
                            ));
                        }
                      : null,
                  icon: const Icon(Icons.exposure_zero),
                  label: const Text('Zero plates'),
                  style: OutlinedButton.styleFrom(
                    padding: const EdgeInsets.symmetric(vertical: 16),
                  ),
                ),
              ),
              if (controller.isRecording) ...[
                const SizedBox(width: 12),
                const Icon(Icons.fiber_manual_record, color: Colors.red, size: 16),
                const SizedBox(width: 4),
                Text(controller.nativeRecording
                    ? 'REC (native)'
                    : 'REC (${controller.recordedRows} rows)'),
              ],
            ],
          ),
          if (controller.lastZeroedAt != null) ...[
            const SizedBox(height: 8),
            Text(
              'Last zeroed at ${_formatTime(controller.lastZeroedAt!)}',
              style: const TextStyle(fontSize: 12, color: Colors.grey),
            ),
          ],
        ],
      ),
    );
  }
}

String _formatTime(DateTime t) {
  String two(int v) => v.toString().padLeft(2, '0');
  return '${two(t.hour)}:${two(t.minute)}:${two(t.second)}';
}

class _StatusCard extends StatelessWidget {
  const _StatusCard({required this.controller});
  final AppController controller;

  @override
  Widget build(BuildContext context) {
    final status = controller.status;
    final error = controller.errorMessage;
    return _SectionCard(
      title: 'Status',
      icon: Icons.info_outline,
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Icon(
                status.streaming ? Icons.check_circle : Icons.circle_outlined,
                color: status.streaming ? Colors.green : Colors.grey,
                size: 18,
              ),
              const SizedBox(width: 8),
              Expanded(child: Text(status.message)),
            ],
          ),
          const SizedBox(height: 6),
          Text('Devices: ${status.deviceCount}   '
              'Frames: ${controller.totalFrames}'),
          if (controller.lastCsvPath != null) ...[
            const SizedBox(height: 6),
            Text('CSV: ${controller.lastCsvPath}',
                style: const TextStyle(fontSize: 12), overflow: TextOverflow.ellipsis),
          ],
          if (error != null) ...[
            const SizedBox(height: 8),
            Container(
              padding: const EdgeInsets.all(10),
              decoration: BoxDecoration(
                color: Colors.red.withValues(alpha: 0.1),
                borderRadius: BorderRadius.circular(8),
              ),
              child: Row(
                children: [
                  const Icon(Icons.error_outline, color: Colors.red, size: 18),
                  const SizedBox(width: 8),
                  Expanded(
                      child: Text(error,
                          style: const TextStyle(color: Colors.red, fontSize: 12))),
                ],
              ),
            ),
          ],
        ],
      ),
    );
  }
}

class _LiveCard extends StatelessWidget {
  const _LiveCard({required this.controller});
  final AppController controller;

  @override
  Widget build(BuildContext context) {
    final status = controller.status;
    final frame = controller.latestFrame;
    final fz = frame?.fz(status.fzIndex);
    final device0 = frame != null && frame.devices.isNotEmpty ? frame.devices.first : null;

    return _SectionCard(
      title: 'Live readout',
      icon: Icons.monitor_heart_outlined,
      child: !controller.showLive
          ? const Text('Live readout is off.')
          : Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Row(
                  crossAxisAlignment: CrossAxisAlignment.baseline,
                  textBaseline: TextBaseline.alphabetic,
                  children: [
                    const Text('FZ  '),
                    Text(
                      fz != null ? fz.toStringAsFixed(2) : '--',
                      style: Theme.of(context).textTheme.displaySmall?.copyWith(
                          fontFeatures: const [], fontWeight: FontWeight.bold),
                    ),
                    const SizedBox(width: 8),
                    const Padding(
                      padding: EdgeInsets.only(bottom: 6),
                      child: Text('N'),
                    ),
                  ],
                ),
                if (device0 != null) ...[
                  const SizedBox(height: 4),
                  Text('Timestamp: ${device0.timestamp}',
                      style: const TextStyle(fontSize: 12)),
                  const Divider(),
                  _ChannelTable(
                    channelNames: status.channelNames,
                    values: device0.channels,
                  ),
                ] else
                  const Padding(
                    padding: EdgeInsets.only(top: 8),
                    child: Text('Waiting for data...'),
                  ),
              ],
            ),
    );
  }
}

class _ChannelTable extends StatelessWidget {
  const _ChannelTable({required this.channelNames, required this.values});

  final List<String> channelNames;
  final List<double> values;

  @override
  Widget build(BuildContext context) {
    return Wrap(
      spacing: 16,
      runSpacing: 8,
      children: [
        for (var i = 0; i < values.length; i++)
          SizedBox(
            width: 120,
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  i < channelNames.length ? channelNames[i] : 'Ch$i',
                  style: const TextStyle(fontSize: 12, color: Colors.grey),
                ),
                Text(values[i].toStringAsFixed(2),
                    style: const TextStyle(
                        fontSize: 16, fontWeight: FontWeight.w600)),
              ],
            ),
          ),
      ],
    );
  }
}
