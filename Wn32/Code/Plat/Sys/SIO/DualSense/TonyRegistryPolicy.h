// Registry policy that plugs into GamepadCore's TBasicDeviceRegistry.
// Maps monotonically-assigned engine IDs down to a small fixed port table
// (SIO::vMAX_PORT entries) so TONYRE's existing port/slot model stays intact.
//
// AllocEngineDevice returns a unique id per physical device path.
// DispatchNewGamepad  claims the first free port for that id + flags it so
//                     the pump can apply defaults (player LED, lightbar).
// DisconnectDevice    releases the port owned by the id.

#pragma once

#include <Sys/sioman.h>

#include <array>
#include <cstddef>
#include <functional>
#include <unordered_map>

namespace DualSensePump
{

struct FTonyRegistryPolicy
{
	using EngineIdType = int;

	struct Hasher
	{
		std::size_t operator()(int K) const noexcept
		{
			return std::hash<int>{}(K);
		}
	};

	static constexpr int kInvalidPort = -1;
	static constexpr int kInvalidEngine = -1;

	int NextEngineId = 0;
	std::array<int, SIO::vMAX_PORT> PortToEngine{}; // kInvalidEngine when free
	std::array<bool, SIO::vMAX_PORT> PortPendingDispatch{};
	std::array<bool, SIO::vMAX_PORT> PortPendingDisconnect{};
	std::unordered_map<int, int, Hasher> EngineToPort;

	FTonyRegistryPolicy()
	{
		PortToEngine.fill(kInvalidEngine);
		PortPendingDispatch.fill(false);
		PortPendingDisconnect.fill(false);
	}

	EngineIdType AllocEngineDevice()
	{
		return NextEngineId++;
	}

	void DispatchNewGamepad(EngineIdType Id)
	{
		for (int Port = 0; Port < SIO::vMAX_PORT; ++Port)
		{
			if (PortToEngine[Port] == kInvalidEngine)
			{
				PortToEngine[Port] = Id;
				EngineToPort[Id] = Port;
				PortPendingDispatch[Port] = true;
				return;
			}
		}
		// All ports full -- pad is known to the registry but inaccessible.
		EngineToPort[Id] = kInvalidPort;
	}

	void DisconnectDevice(EngineIdType Id)
	{
		auto It = EngineToPort.find(Id);
		if (It == EngineToPort.end()) return;
		const int Port = It->second;
		if (Port >= 0 && Port < SIO::vMAX_PORT)
		{
			PortToEngine[Port] = kInvalidEngine;
			PortPendingDisconnect[Port] = true;
		}
		EngineToPort.erase(It);
	}

	int GetEngineForPort(int Port) const
	{
		if (Port < 0 || Port >= SIO::vMAX_PORT) return kInvalidEngine;
		return PortToEngine[Port];
	}
};

} // namespace DualSensePump
