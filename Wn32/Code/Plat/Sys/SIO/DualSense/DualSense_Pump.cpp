// See DualSense_Pump.h.

#include "DualSense_Pump.h"

#include "FHIDDeviceInfo.h"
#include "TonyRegistryPolicy.h"

#include <GCore/Interfaces/ISonyGamepad.h>
#include <GCore/Interfaces/IPlatformHardwareInfo.h>
#include <GCore/Templates/TBasicDeviceRegistry.h>
#include <GCore/Types/DSCoreTypes.h>
#include <GCore/Types/ECoreGamepad.h>

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace DualSensePump
{

namespace
{
	using FRegistry = GamepadCore::TBasicDeviceRegistry<FTonyRegistryPolicy>;

	FRegistry* g_Registry = nullptr;

	DSCoreTypes::FDSColor DefaultLightbarForPort(int Port)
	{
		switch (Port)
		{
		case 0:  return {0,   80,  255, 1}; // blue
		case 1:  return {255, 40,  0,   1}; // red
		case 2:  return {0,   200, 60,  1}; // green
		case 3:  return {255, 180, 0,   1}; // yellow
		default: return {128, 128, 128, 1};
		}
	}

	EDSPlayer LedForPort(int Port)
	{
		switch (Port)
		{
		case 0:  return EDSPlayer::One;
		case 1:  return EDSPlayer::Two;
		case 2:  return EDSPlayer::Three;
		case 3:  return EDSPlayer::All;
		default: return EDSPlayer::Off;
		}
	}

	std::uint8_t AnalogToByte(float V)
	{
		// V in [-1, 1]. Center maps to 0x80 like the keyboard fallback.
		float Scaled = 0.5f + 0.5f * V;
		if (Scaled < 0.0f) Scaled = 0.0f;
		if (Scaled > 1.0f) Scaled = 1.0f;
		int I = static_cast<int>(Scaled * 255.0f + 0.5f);
		if (I < 0) I = 0; else if (I > 255) I = 255;
		return static_cast<std::uint8_t>(I);
	}

	std::uint8_t TriggerToByte(float V)
	{
		if (V < 0.0f) V = 0.0f;
		if (V > 1.0f) V = 1.0f;
		int I = static_cast<int>(V * 255.0f + 0.5f);
		if (I < 0) I = 0; else if (I > 255) I = 255;
		return static_cast<std::uint8_t>(I);
	}

	EDSGamepadHand MapHand(int Hand)
	{
		switch (Hand)
		{
		case 0:  return EDSGamepadHand::Left;
		case 1:  return EDSGamepadHand::Right;
		default: return EDSGamepadHand::AnyHand;
		}
	}
}

void Init()
{
	if (g_Registry) return;
	IPlatformHardwareInfo::SetInstance(std::make_unique<FHIDDeviceInfo>());
	g_Registry = new FRegistry();
	// First pass so pads plugged in at boot are ready immediately.
	g_Registry->RequestImmediateDetection();
	g_Registry->PlugAndPlay(0.0f);

	// Apply defaults for anything dispatched during the boot pass.
	for (int Port = 0; Port < SIO::vMAX_PORT; ++Port)
	{
		if (!g_Registry->Policy.PortPendingDispatch[Port]) continue;
		ISonyGamepad* Pad = g_Registry->GetLibrary(g_Registry->Policy.GetEngineForPort(Port));
		if (Pad)
		{
			Pad->SetPlayerLed(LedForPort(Port), 255);
			Pad->SetLightbar(DefaultLightbarForPort(Port));
			Pad->UpdateOutput();
		}
		g_Registry->Policy.PortPendingDispatch[Port] = false;
	}
}

void Shutdown()
{
	if (g_Registry)
	{
		delete g_Registry;
		g_Registry = nullptr;
	}
	IPlatformHardwareInfo::SetInstance(nullptr);
}

void Tick(float DeltaTime)
{
	if (!g_Registry) return;
	g_Registry->PlugAndPlay(DeltaTime);

	for (int Port = 0; Port < SIO::vMAX_PORT; ++Port)
	{
		if (g_Registry->Policy.PortPendingDispatch[Port])
		{
			ISonyGamepad* Pad = g_Registry->GetLibrary(g_Registry->Policy.GetEngineForPort(Port));
			if (Pad)
			{
				Pad->SetPlayerLed(LedForPort(Port), 255);
				Pad->SetLightbar(DefaultLightbarForPort(Port));
				Pad->UpdateOutput();
			}
			g_Registry->Policy.PortPendingDispatch[Port] = false;
		}
		g_Registry->Policy.PortPendingDisconnect[Port] = false;
	}

	// Pump HID at top-of-frame so read_data() consumes a fresh report instead
	// of one that was read mid-previous-frame. Cuts ~1 frame of input delay.
	for (int Port = 0; Port < SIO::vMAX_PORT; ++Port)
	{
		ISonyGamepad* Pad = GetPadForPort(Port);
		if (Pad && Pad->IsConnected())
		{
			Pad->UpdateInput(DeltaTime);
		}
	}
}

ISonyGamepad* GetPadForPort(int Port)
{
	if (!g_Registry) return nullptr;
	const int Engine = g_Registry->Policy.GetEngineForPort(Port);
	if (Engine == FTonyRegistryPolicy::kInvalidEngine) return nullptr;
	return g_Registry->GetLibrary(Engine);
}

void PumpInputToPS2Buffer(ISonyGamepad* Pad, unsigned char Out[32])
{
	if (!Pad) return;
	FDeviceContext* Ctx = Pad->GetMutableDeviceContext();
	if (!Ctx) return;
	FInputContext* In = Ctx->GetInputState();
	if (!In) return;

	// Header bytes match the keyboard-fallback output in read_data().
	Out[0] = 0x00;
	Out[1] = static_cast<unsigned char>((0x07 << 4) | 16);

	// Buttons use active-low: default 0xFF, clear bit on press.
	unsigned char B2 = 0xFF;
	unsigned char B3 = 0xFF;

	if (In->bStart)       B2 &= ~(1 << 3);
	if (In->bShare)       B2 &= ~(1 << 0);
	if (In->bLeftStick)   B2 &= ~(1 << 1);
	if (In->bRightStick)  B2 &= ~(1 << 2);
	if (In->bDpadUp)      B2 &= ~(1 << 4);
	if (In->bDpadRight)   B2 &= ~(1 << 5);
	if (In->bDpadDown)    B2 &= ~(1 << 6);
	if (In->bDpadLeft)    B2 &= ~(1 << 7);

	if (In->bTriangle)    B3 &= ~(1 << 4);
	if (In->bCircle)      B3 &= ~(1 << 5);
	if (In->bCross)       B3 &= ~(1 << 6);
	if (In->bSquare)      B3 &= ~(1 << 7);
	// Trigger digital bit: hardware click (bXTriggerThreshold) only trips past
	// ~50% travel, so a light tap never registers. OR in an analog threshold
	// above the lib's AnalogDeadZone (0.08) but below the click point.
	constexpr float kTriggerDigitalThreshold = 0.12f;
	if (In->bLeftTriggerThreshold  || In->LeftTriggerAnalog  > kTriggerDigitalThreshold) B3 &= ~(1 << 0); // L2
	if (In->bRightTriggerThreshold || In->RightTriggerAnalog > kTriggerDigitalThreshold) B3 &= ~(1 << 1); // R2
	if (In->bLeftShoulder)          B3 &= ~(1 << 2); // L1
	if (In->bRightShoulder)         B3 &= ~(1 << 3); // R1

	Out[2] = B2;
	Out[3] = B3;

	// Analog sticks: byte[4-5] = right XY, byte[6-7] = left XY.
	// GamepadInput parser does `(raw - 128) / -128` on Y, so FInputContext Y is
	// +1 when the stick is pushed up. PS2 layout expects 0x00 == up / 0xFF == down,
	// so negate Y to match. X is already parsed un-negated (+X == right == 0xFF).
	Out[4] = AnalogToByte( In->RightAnalog.X);
	Out[5] = AnalogToByte(-In->RightAnalog.Y);
	Out[6] = AnalogToByte( In->LeftAnalog.X);
	Out[7] = AnalogToByte(-In->LeftAnalog.Y);

	// DPad pressure: match existing [8-11] = R/L/U/D.
	Out[8]  = In->bDpadRight ? 0xFF : 0x00;
	Out[9]  = In->bDpadLeft  ? 0xFF : 0x00;
	Out[10] = In->bDpadUp    ? 0xFF : 0x00;
	Out[11] = In->bDpadDown  ? 0xFF : 0x00;

	// Face + shoulder pressure. Game may or may not read these on PC, but they
	// cost nothing to fill.
	Out[12] = In->bTriangle ? 0xFF : 0x00;
	Out[13] = In->bCircle   ? 0xFF : 0x00;
	Out[14] = In->bCross    ? 0xFF : 0x00;
	Out[15] = In->bSquare   ? 0xFF : 0x00;
	Out[16] = In->bLeftShoulder  ? 0xFF : 0x00;
	Out[17] = In->bRightShoulder ? 0xFF : 0x00;
	Out[18] = TriggerToByte(In->LeftTriggerAnalog);
	Out[19] = TriggerToByte(In->RightTriggerAnalog);

	Out[20] = 0x00;
	// [21..31] reserved / unused on this path.
	for (int I = 21; I < 32; ++I) Out[I] = 0x00;
}

void SetRumble(int Port, std::uint8_t Left, std::uint8_t Right)
{
	ISonyGamepad* Pad = GetPadForPort(Port);
	if (!Pad) return;
	Pad->SetVibration(Left, Right);
	Pad->UpdateOutput();
}

void SetLightbar(int Port, std::uint8_t R, std::uint8_t G, std::uint8_t B)
{
	ISonyGamepad* Pad = GetPadForPort(Port);
	if (!Pad) return;
	DSCoreTypes::FDSColor C;
	C.R = R; C.G = G; C.B = B; C.A = 1;
	Pad->SetLightbar(C);
	Pad->UpdateOutput();
}

void SetAdaptiveTriggerPreset(int Port, int Hand, int Preset)
{
	ISonyGamepad* Pad = GetPadForPort(Port);
	if (!Pad) return;
	IGamepadTrigger* T = Pad->GetIGamepadTrigger();
	if (!T) return; // DualShock 4 has no adaptive triggers.

	const EDSGamepadHand H = MapHand(Hand);
	switch (Preset)
	{
	case 1: T->SetBow22(0x10, 0x80, H); break;                       // bow
	case 2: T->SetWeapon25(0x04, 0xFF, 0x00, 0x00, H); break;        // weapon
	case 3: T->SetMachineGun26(0x02, 0x27, 0xFF, 0x08, H); break;    // machine gun
	case 4: T->SetGalloping23(0x00, 0x09, 0x02, 0x05, 0x08, H); break; // gallop
	case 5: T->SetGameCube(H); break;                                 // gamecube
	case 0:
	default: T->StopTrigger(H); break;                                // reset
	}
	Pad->UpdateOutput();
}

void SetPlayerLed(int Port, int ZeroBased)
{
	ISonyGamepad* Pad = GetPadForPort(Port);
	if (!Pad) return;
	if (ZeroBased < 0) ZeroBased = 0;
	if (ZeroBased > 3) ZeroBased = 3;
	Pad->SetPlayerLed(LedForPort(ZeroBased), 255);
	Pad->UpdateOutput();
}

bool GetTouchpad(int Port, float& OutX, float& OutY, bool& OutTouching)
{
	ISonyGamepad* Pad = GetPadForPort(Port);
	if (!Pad) return false;
	FDeviceContext* Ctx = Pad->GetMutableDeviceContext();
	if (!Ctx) return false;
	FInputContext* In = Ctx->GetInputState();
	if (!In) return false;
	OutX = In->TouchPosition.X;
	OutY = In->TouchPosition.Y;
	OutTouching = In->bIsTouching;
	return true;
}

bool GetGyro(int Port, float& OutX, float& OutY, float& OutZ)
{
	ISonyGamepad* Pad = GetPadForPort(Port);
	if (!Pad) return false;
	FDeviceContext* Ctx = Pad->GetMutableDeviceContext();
	if (!Ctx) return false;
	FInputContext* In = Ctx->GetInputState();
	if (!In) return false;
	OutX = In->Gyroscope.X;
	OutY = In->Gyroscope.Y;
	OutZ = In->Gyroscope.Z;
	return true;
}

} // namespace DualSensePump
