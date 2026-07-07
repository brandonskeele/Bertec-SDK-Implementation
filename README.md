# Bertec-SDK-Implementation

A Windows Flutter app that records Bertec force-plate data. It offers two
selectable data-collection paths and independent toggles for a live on-screen
readout and CSV recording.

## Data source switch

The GUI lets you choose which code drives data collection:

- **Custom (Dart FFI)** - a pure-Dart implementation that loads
  `BertecDevice.dll` directly via `dart:ffi` and runs the full SDK lifecycle
  (`bertec_Init` -> `bertec_Start` -> wait for `BERTEC_DEVICES_READY` ->
  `bertec_StartDataStream` -> poll `bertec_ReadBufferedDataStream`).
- **Example (vendor C++)** - a native bridge (`bertec_bridge.dll`) that
  reproduces the SDK call sequence of the vendor example programs and streams
  frames back to the app. A dropdown selects which example to run:
  - `SimpleFZReaderExample.cpp`
  - `BertecExample.cpp`
  - `InternalClockingFZReaderExample.cpp`
  - `AuxPinDataCollectionStartStopExample.cpp`
  - `InternalClockingFZReaderExampleMultipleSync.cpp`

Both paths poll the native side on a timer and feed the same UI and recorder.

## Recording

- **Show live readout** - displays the running FZ value, timestamp, and all
  channel values. Purely visual; does not affect recording.
- **Save CSV** - while streaming, writes every frame to a timestamped CSV file
  in the chosen output folder (defaults to `Documents/BertecRecordings`).

Each toggle works independently: you can show live data without saving,
save without showing live data, both, or neither.

## Requirements

- **Windows** (this project targets Windows only).
- **Flutter** (stable) with Windows desktop support and Visual Studio C++ build
  tools.
- **FTDI USB driver (`ftd2xx`)** installed for your Bertec device. Without it,
  `bertec_Init` fails. Installing Bertec Digital Acquire is the easiest way to
  get the driver; see `Bertec_Device_SDK_March_2026/Readme.txt`.
- A connected Bertec force plate/amplifier. The advanced example variants
  (internal-clock multi-sync, aux start/stop) require hardware that supports
  those SYNC/AUX features to actually stream.

## Native SDK integration

The Bertec SDK lives in `Bertec_Device_SDK_March_2026/`. The build:

- compiles `windows/bertec/bertec_bridge.cpp` into `bertec_bridge.dll`,
  linking `Bertec_Device_SDK_March_2026/x64/BertecDevice.lib`;
- copies `BertecDevice.dll`, `ftd2xx.dll`, and `bertec_bridge.dll` next to the
  built executable (and bundles them on install) so both `flutter run` and
  packaged builds can find them.

The Dart FFI struct layouts in `lib/src/bertec/bindings.dart` mirror
`bertecif.h` (`#pragma pack(8)`) and `windows/bertec/bertec_bridge.h`.

## Build and run

```powershell
flutter pub get
flutter run -d windows      # debug run
flutter build windows       # release build
```

The release executable and its DLLs are produced under
`build\windows\x64\runner\Release\`.

## Project layout

- `lib/main.dart` - app entry point.
- `lib/src/bertec/` - FFI bindings, source interface, custom and bridge sources.
- `lib/src/recording/csv_recorder.dart` - CSV writer.
- `lib/src/state/app_controller.dart` - orchestration and app state.
- `lib/src/ui/home_page.dart` - user interface.
- `windows/bertec/` - native example bridge.
