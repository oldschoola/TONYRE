// TONYRE Windows HID policy for GamepadCore (DualSense / DualShock 4).
// Implements IPlatformHardwareInfo via SetupAPI + hid.dll.
//
// Detect()   : enumerate Sony (VID 0x054C) HID devices, filter by PID,
//              classify USB vs Bluetooth from input report length.
// CreateHandle / InvalidateHandle : open/close overlapped file handle per path.
// Read()     : non-blocking overlapped ReadFile; keeps one pending read per
//              handle so UpdateInput can poll each frame without stalling.
// Write()    : sync WriteFile with report size derived from DeviceType +
//              ConnectionType.
// Audio*()   : no-op (GAMEPAD_CORE_HAS_AUDIO=0).

#pragma once

#include "GCore/Interfaces/IPlatformHardwareInfo.h"

#include <memory>

class FHIDDeviceInfo final : public IPlatformHardwareInfo
{
public:
	FHIDDeviceInfo();
	~FHIDDeviceInfo() override;

	void Read(FDeviceContext* Context) override;
	void Write(FDeviceContext* Context) override;
	void Detect(std::vector<FDeviceContext>& Devices) override;
	bool CreateHandle(FDeviceContext* Context) override;
	void InvalidateHandle(FDeviceContext* Context) override;
	void ProcessAudioHaptic(FDeviceContext* /*Context*/) override {}
	void InitializeAudioDevice(FDeviceContext* /*Context*/) override {}

private:
	struct FImpl;
	std::unique_ptr<FImpl> Impl;
};
