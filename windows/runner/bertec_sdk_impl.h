#ifndef RUNNER_WINDOWS_BERTEC_H_
#define RUNNER_WINDOWS_BERTEC_H_

#include <flutter/binary_messenger.h>

#include <windows.h>

// Custom window message used to marshal Bertec EventChannel payloads onto the
// Flutter platform thread.
constexpr UINT WM_BERTEC_DISPATCH = WM_APP + 100;

// Registers MethodChannel `app/bertec` and EventChannel
// `app/bertec_force_stream` for Bertec force plate USB integration.
void WindowsBertecRegister(flutter::BinaryMessenger* messenger);

// Routes EventChannel emissions through the runner's HWND message pump.
void WindowsBertecSetTargetWindow(HWND hwnd);

// Drain queued Bertec events on the platform thread. Returns true if handled.
bool WindowsBertecHandleMessage(UINT message);

#endif  // RUNNER_WINDOWS_BERTEC_H_
