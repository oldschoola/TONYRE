// Bridge between GamepadCore's DualSense/DualShock library and TONYRE's SIO
// port model. Owns the IPlatformHardwareInfo singleton + the registry, and
// exposes per-port helpers that the SIO Device uses to fill the PS2-style
// control buffer and drive output features (rumble, adaptive triggers,
// lightbar, player LED).

#pragma once

#include <cstdint>

class ISonyGamepad;

namespace DualSensePump
{

// Called once at boot. Installs FHIDDeviceInfo as the platform HID policy
// and builds the registry. Runs a synchronous first detection pass so pads
// plugged in at launch are available immediately.
void Init();

// Called at shutdown to tear down the registry before the policy singleton.
void Shutdown();

// Per-frame. Runs detection/reconnect, applies default lightbar+LED for newly
// dispatched pads, and pumps UpdateInput for each connected pad so the
// library's input buffer is current before any port reads it.
void Tick(float DeltaTime);

// Returns the library instance for the given port, or nullptr if that port
// has no pad bound.
ISonyGamepad* GetPadForPort(int Port);

// Maps FInputContext (front buffer, mutex-guarded) to a PS2/DUALSHOCK2 layout
// compatible with SIO::Device::m_data.m_control_data[32]. Caller supplies
// the 32-byte target buffer; helper matches the bit layout currently emitted
// by the keyboard fallback in p_siodev.cpp read_data().
void PumpInputToPS2Buffer(ISonyGamepad* Pad, unsigned char Out[32]);

// Output helpers. Each stages the change on the pad's OutputContext and
// triggers a single UpdateOutput() so the HID report is flushed to the
// device. Caller must supply a valid port with an attached pad.
void SetRumble(int Port, std::uint8_t Left, std::uint8_t Right);
void SetLightbar(int Port, std::uint8_t R, std::uint8_t G, std::uint8_t B);

// Hand: 0 = Left trigger, 1 = Right trigger, 2 = both.
// Preset: 0=reset, 1=bow, 2=weapon, 3=machinegun, 4=galloping, 5=gamecube.
void SetAdaptiveTriggerPreset(int Port, int Hand, int Preset);

// ZeroBased: 0..3 (clamped). Maps to EDSPlayer LED masks.
void SetPlayerLed(int Port, int ZeroBased);

// Touchpad / IMU query (reads the front input buffer via GetInputState()).
bool GetTouchpad(int Port, float& OutX, float& OutY, bool& OutTouching);
bool GetGyro(int Port, float& OutX, float& OutY, float& OutZ);

} // namespace DualSensePump
