// See FAsyncCachedHIDDeviceInfo.h.

#include "FAsyncCachedHIDDeviceInfo.h"
#include "FHIDDeviceInfo.h"

#include <utility>

FAsyncCachedHIDDeviceInfo::FAsyncCachedHIDDeviceInfo()
	: m_inner(std::make_unique<FHIDDeviceInfo>())
{
	// Seed the snapshot synchronously so the registry's boot-time
	// RequestImmediateDetection() finds whatever is plugged in immediately.
	m_inner->Detect(m_snapshot);

	m_worker = std::thread(&FAsyncCachedHIDDeviceInfo::WorkerThreadMain, this);
}

FAsyncCachedHIDDeviceInfo::~FAsyncCachedHIDDeviceInfo()
{
	m_stop.store(true, std::memory_order_release);
	if (m_worker.joinable())
	{
		m_worker.join();
	}
}

void FAsyncCachedHIDDeviceInfo::WorkerThreadMain()
{
	// Coarse poll: re-scan every kDetectionPeriod, but wake quickly on shutdown
	// by sleeping in short slices and rechecking m_stop.
	using clock = std::chrono::steady_clock;
	const auto slice = std::chrono::milliseconds(50);

	while (!m_stop.load(std::memory_order_acquire))
	{
		auto next_scan = clock::now() + kDetectionPeriod;
		while (clock::now() < next_scan)
		{
			if (m_stop.load(std::memory_order_acquire)) return;
			std::this_thread::sleep_for(slice);
		}

		std::vector<FDeviceContext> tmp;
		m_inner->Detect(tmp);

		{
			std::lock_guard<std::mutex> lock(m_snapshot_mutex);
			m_snapshot = std::move(tmp);
		}
	}
}

void FAsyncCachedHIDDeviceInfo::Detect(std::vector<FDeviceContext>& Devices)
{
	std::lock_guard<std::mutex> lock(m_snapshot_mutex);
	Devices = m_snapshot;
}

void FAsyncCachedHIDDeviceInfo::Read(FDeviceContext* Context)
{
	m_inner->Read(Context);
}

void FAsyncCachedHIDDeviceInfo::Write(FDeviceContext* Context)
{
	m_inner->Write(Context);
}

bool FAsyncCachedHIDDeviceInfo::CreateHandle(FDeviceContext* Context)
{
	return m_inner->CreateHandle(Context);
}

void FAsyncCachedHIDDeviceInfo::InvalidateHandle(FDeviceContext* Context)
{
	m_inner->InvalidateHandle(Context);
}

void FAsyncCachedHIDDeviceInfo::ProcessAudioHaptic(FDeviceContext* Context)
{
	m_inner->ProcessAudioHaptic(Context);
}

void FAsyncCachedHIDDeviceInfo::InitializeAudioDevice(FDeviceContext* Context)
{
	m_inner->InitializeAudioDevice(Context);
}
