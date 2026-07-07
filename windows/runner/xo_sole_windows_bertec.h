#ifndef RUNNER_XO_SOLE_WINDOWS_BERTEC_H_
#define RUNNER_XO_SOLE_WINDOWS_BERTEC_H_

#include <flutter/binary_messenger.h>

#include <windows.h>

// Custom window message used to marshal Bertec EventChannel payloads onto the
// Flutter platform thread.
constexpr UINT WM_XO_SOLE_BERTEC_DISPATCH = WM_APP + 100;

// Registers MethodChannel `xo_sole/bertec` and EventChannel
// `xo_sole/bertec_force_stream` for Bertec force plate USB integration.
void XoSoleWindowsBertecRegister(flutter::BinaryMessenger* messenger);

// Routes EventChannel emissions through the runner's HWND message pump.
void XoSoleWindowsBertecSetTargetWindow(HWND hwnd);

// Drain queued Bertec events on the platform thread. Returns true if handled.
bool XoSoleWindowsBertecHandleMessage(UINT message);

#endif  // RUNNER_XO_SOLE_WINDOWS_BERTEC_H_
