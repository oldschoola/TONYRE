// Async-cached IPlatformHardwareInfo wrapper.
//
// FHIDDeviceInfo::Detect() walks the SetupDi HID tree on every call (~60 ms
// on Windows). TBasicDeviceRegistry::PlugAndPlay() invokes Detect on the main
// thread once per second, producing a periodic frametime spike at exactly that
// cadence (62-65 ms, every ~120 frames @ 120 fps).
//
// This adapter delegates Read/Write/CreateHandle/InvalidateHandle straight to
// an inner FHIDDeviceInfo (those operate on already-open per-device handles
// and must keep happening on the main thread). Detect() is hoisted to a
// background worker that re-scans every kDetectionPeriod and publishes the
// result to a mutex-guarded snapshot. Main-thread Detect() then copies the
// snapshot in O(num_pads) — cheap.
//
// The first snapshot is seeded synchronously at construction so the registry's
// boot-time RequestImmediateDetection() path still finds connected pads.

#pragma once

#include "GCore/Interfaces/IPlatformHardwareInfo.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

class FHIDDeviceInfo;

class FAsyncCachedHIDDeviceInfo final : public IPlatformHardwareInfo
{
public:
	FAsyncCachedHIDDeviceInfo();
	~FAsyncCachedHIDDeviceInfo() override;

	void Read(FDeviceContext* Context) override;
	void Write(FDeviceContext* Context) override;
	void Detect(std::vector<FDeviceContext>& Devices) override;
	bool CreateHandle(FDeviceContext* Context) override;
	void InvalidateHandle(FDeviceContext* Context) override;
	void ProcessAudioHaptic(FDeviceContext* Context) override;
	void InitializeAudioDevice(FDeviceContext* Context) override;

	static constexpr std::chrono::milliseconds kDetectionPeriod{1000};

private:
	void WorkerThreadMain();

	std::unique_ptr<FHIDDeviceInfo>		m_inner;

	std::mutex							m_snapshot_mutex;
	std::vector<FDeviceContext>			m_snapshot;

	std::atomic<bool>					m_stop{false};
	std::thread							m_worker;
};
