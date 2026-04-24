// TONYRE Windows HID policy implementation.
// See FHIDDeviceInfo.h for semantics.

#include "FHIDDeviceInfo.h"

#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <hidpi.h>

#include <algorithm>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>

namespace
{
	constexpr unsigned short kSonyVID               = 0x054C;
	constexpr unsigned short kPID_DualSense         = 0x0CE6;
	constexpr unsigned short kPID_DualSenseEdge     = 0x0DF2;
	constexpr unsigned short kPID_DualShock4_v1     = 0x05C4;
	constexpr unsigned short kPID_DualShock4_v2     = 0x09CC;

	std::string WideToUtf8(const wchar_t* W)
	{
		if (!W || !*W) return {};
		int Len = ::WideCharToMultiByte(CP_UTF8, 0, W, -1, nullptr, 0, nullptr, nullptr);
		if (Len <= 1) return {};
		std::string Out(static_cast<size_t>(Len - 1), '\0');
		::WideCharToMultiByte(CP_UTF8, 0, W, -1, Out.data(), Len, nullptr, nullptr);
		return Out;
	}

	EDSDeviceType ClassifyPid(unsigned short Pid)
	{
		switch (Pid)
		{
		case kPID_DualSense:      return EDSDeviceType::DualSense;
		case kPID_DualSenseEdge:  return EDSDeviceType::DualSenseEdge;
		case kPID_DualShock4_v1:
		case kPID_DualShock4_v2:  return EDSDeviceType::DualShock4;
		default:                  return EDSDeviceType::NotFound;
		}
	}

	// USB vs Bluetooth is inferred from the input report byte length reported
	// by the HID descriptor:
	//   DualSense USB: 64, DualSense BT: 78
	//   DualShock4 USB: 64, DualShock4 BT: 547
	EDSDeviceConnection ClassifyConnection(EDSDeviceType Type, unsigned long InputReportBytes)
	{
		if (Type == EDSDeviceType::DualShock4)
		{
			if (InputReportBytes >= 100) return EDSDeviceConnection::Bluetooth;
			return EDSDeviceConnection::Usb;
		}
		// DualSense variants: 78 = BT, 64 = USB.
		if (InputReportBytes >= 70) return EDSDeviceConnection::Bluetooth;
		return EDSDeviceConnection::Usb;
	}
}

struct FHIDDeviceInfo::FImpl
{
	struct FHandleEntry
	{
		HANDLE FileHandle = INVALID_HANDLE_VALUE;
		HANDLE ReadEvent = nullptr;
		OVERLAPPED ReadOv{};
		bool bReadPending = false;
		unsigned long InputReportBytes = 0;
	};

	std::mutex Mutex;
	std::unordered_map<std::string, FHandleEntry> Handles;

	static unsigned long GetWriteSize(const FDeviceContext* C)
	{
		const bool Bt = C->ConnectionType == EDSDeviceConnection::Bluetooth;
		switch (C->DeviceType)
		{
		case EDSDeviceType::DualSense:
		case EDSDeviceType::DualSenseEdge:
			return Bt ? 78u : 48u;
		case EDSDeviceType::DualShock4:
			return Bt ? 78u : 32u;
		default:
			return 64u;
		}
	}

	static unsigned long GetReadSize(const FDeviceContext* C, const FHandleEntry& E)
	{
		// Prefer the HID-reported length when available so we don't truncate
		// partial reports; otherwise fall back to canonical sizes.
		if (E.InputReportBytes > 0) return E.InputReportBytes;
		const bool Bt = C->ConnectionType == EDSDeviceConnection::Bluetooth;
		switch (C->DeviceType)
		{
		case EDSDeviceType::DualSense:
		case EDSDeviceType::DualSenseEdge:
			return Bt ? 78u : 64u;
		case EDSDeviceType::DualShock4:
			return Bt ? 547u : 64u;
		default:
			return 64u;
		}
	}

	void CloseEntry_NoLock(FHandleEntry& E)
	{
		if (E.bReadPending && E.FileHandle != INVALID_HANDLE_VALUE)
		{
			::CancelIoEx(E.FileHandle, &E.ReadOv);
			DWORD Bytes = 0;
			::GetOverlappedResult(E.FileHandle, &E.ReadOv, &Bytes, TRUE);
			E.bReadPending = false;
		}
		if (E.FileHandle != INVALID_HANDLE_VALUE)
		{
			::CloseHandle(E.FileHandle);
			E.FileHandle = INVALID_HANDLE_VALUE;
		}
		if (E.ReadEvent != nullptr)
		{
			::CloseHandle(E.ReadEvent);
			E.ReadEvent = nullptr;
		}
	}
};

FHIDDeviceInfo::FHIDDeviceInfo()
	: Impl(std::make_unique<FImpl>())
{
}

FHIDDeviceInfo::~FHIDDeviceInfo()
{
	std::lock_guard<std::mutex> Lock(Impl->Mutex);
	for (auto& KV : Impl->Handles)
	{
		Impl->CloseEntry_NoLock(KV.second);
	}
	Impl->Handles.clear();
}

void FHIDDeviceInfo::Detect(std::vector<FDeviceContext>& Devices)
{
	GUID HidGuid{};
	::HidD_GetHidGuid(&HidGuid);

	HDEVINFO DevInfo = ::SetupDiGetClassDevsW(
		&HidGuid,
		nullptr,
		nullptr,
		DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
	if (DevInfo == INVALID_HANDLE_VALUE) return;

	SP_DEVICE_INTERFACE_DATA IfData{};
	IfData.cbSize = sizeof(IfData);

	for (DWORD Index = 0; ; ++Index)
	{
		if (!::SetupDiEnumDeviceInterfaces(DevInfo, nullptr, &HidGuid, Index, &IfData))
		{
			break;
		}

		DWORD Required = 0;
		::SetupDiGetDeviceInterfaceDetailW(DevInfo, &IfData, nullptr, 0, &Required, nullptr);
		if (Required == 0) continue;

		std::vector<unsigned char> DetailBuf(Required, 0);
		auto* Detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(DetailBuf.data());
		Detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

		if (!::SetupDiGetDeviceInterfaceDetailW(
				DevInfo, &IfData, Detail, Required, nullptr, nullptr))
		{
			continue;
		}

		// Briefly open with no access to query attributes + caps.
		HANDLE Probe = ::CreateFileW(
			Detail->DevicePath,
			0,
			FILE_SHARE_READ | FILE_SHARE_WRITE,
			nullptr,
			OPEN_EXISTING,
			0,
			nullptr);
		if (Probe == INVALID_HANDLE_VALUE) continue;

		HIDD_ATTRIBUTES Attr{};
		Attr.Size = sizeof(Attr);
		if (!::HidD_GetAttributes(Probe, &Attr) || Attr.VendorID != kSonyVID)
		{
			::CloseHandle(Probe);
			continue;
		}

		const EDSDeviceType Type = ClassifyPid(Attr.ProductID);
		if (Type == EDSDeviceType::NotFound)
		{
			::CloseHandle(Probe);
			continue;
		}

		unsigned long InputReportBytes = 0;
		PHIDP_PREPARSED_DATA Preparsed = nullptr;
		if (::HidD_GetPreparsedData(Probe, &Preparsed))
		{
			HIDP_CAPS Caps{};
			if (::HidP_GetCaps(Preparsed, &Caps) == HIDP_STATUS_SUCCESS)
			{
				InputReportBytes = Caps.InputReportByteLength;
			}
			::HidD_FreePreparsedData(Preparsed);
		}

		::CloseHandle(Probe);

		FDeviceContext Ctx;
		Ctx.Path = WideToUtf8(Detail->DevicePath);
		Ctx.DeviceType = Type;
		Ctx.ConnectionType = ClassifyConnection(Type, InputReportBytes);
		Ctx.Handle = INVALID_PLATFORM_HANDLE;
		Ctx.IsConnected = false;
		Devices.push_back(std::move(Ctx));
	}

	::SetupDiDestroyDeviceInfoList(DevInfo);
}

bool FHIDDeviceInfo::CreateHandle(FDeviceContext* Context)
{
	if (!Context || Context->Path.empty()) return false;

	std::lock_guard<std::mutex> Lock(Impl->Mutex);

	// Reuse existing handle for the same path (registry re-probes every second
	// and would otherwise leak an OS handle per tick).
	auto It = Impl->Handles.find(Context->Path);
	if (It != Impl->Handles.end() && It->second.FileHandle != INVALID_HANDLE_VALUE)
	{
		Context->Handle = It->second.FileHandle;
		Context->IsConnected = true;
		return true;
	}

	const int WideNeeded = ::MultiByteToWideChar(CP_UTF8, 0, Context->Path.c_str(), -1, nullptr, 0);
	if (WideNeeded <= 0) return false;
	std::wstring WidePath(static_cast<size_t>(WideNeeded - 1), L'\0');
	::MultiByteToWideChar(CP_UTF8, 0, Context->Path.c_str(), -1, WidePath.data(), WideNeeded);

	HANDLE File = ::CreateFileW(
		WidePath.c_str(),
		GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		nullptr,
		OPEN_EXISTING,
		FILE_FLAG_OVERLAPPED,
		nullptr);
	if (File == INVALID_HANDLE_VALUE) return false;

	FImpl::FHandleEntry Entry{};
	Entry.FileHandle = File;
	Entry.ReadEvent = ::CreateEventW(nullptr, TRUE /*manual reset*/, FALSE, nullptr);
	if (Entry.ReadEvent == nullptr)
	{
		::CloseHandle(File);
		return false;
	}
	Entry.ReadOv.hEvent = Entry.ReadEvent;
	Entry.bReadPending = false;

	// Cache input report length so Read() knows how much to request.
	PHIDP_PREPARSED_DATA Preparsed = nullptr;
	if (::HidD_GetPreparsedData(File, &Preparsed))
	{
		HIDP_CAPS Caps{};
		if (::HidP_GetCaps(Preparsed, &Caps) == HIDP_STATUS_SUCCESS)
		{
			Entry.InputReportBytes = Caps.InputReportByteLength;
		}
		::HidD_FreePreparsedData(Preparsed);
	}

	Impl->Handles[Context->Path] = Entry;
	Context->Handle = File;
	Context->IsConnected = true;
	return true;
}

void FHIDDeviceInfo::InvalidateHandle(FDeviceContext* Context)
{
	if (!Context) return;
	std::lock_guard<std::mutex> Lock(Impl->Mutex);
	auto It = Impl->Handles.find(Context->Path);
	if (It != Impl->Handles.end())
	{
		Impl->CloseEntry_NoLock(It->second);
		Impl->Handles.erase(It);
	}
	Context->Handle = INVALID_PLATFORM_HANDLE;
	Context->IsConnected = false;
}

void FHIDDeviceInfo::Read(FDeviceContext* Context)
{
	if (!Context) return;

	std::lock_guard<std::mutex> Lock(Impl->Mutex);
	auto It = Impl->Handles.find(Context->Path);
	if (It == Impl->Handles.end()) return;

	auto& Entry = It->second;
	if (Entry.FileHandle == INVALID_HANDLE_VALUE) return;

	// Pick destination buffer: DualShock4 BT uses BufferDS4[547], everything
	// else fits in Buffer[78].
	unsigned char* Buffer;
	unsigned long BufferCap;
	if (Context->DeviceType == EDSDeviceType::DualShock4 &&
	    Context->ConnectionType == EDSDeviceConnection::Bluetooth)
	{
		Buffer = Context->BufferDS4;
		BufferCap = static_cast<unsigned long>(sizeof(Context->BufferDS4));
	}
	else
	{
		Buffer = Context->Buffer;
		BufferCap = static_cast<unsigned long>(sizeof(Context->Buffer));
	}

	unsigned long RequestSize = FImpl::GetReadSize(Context, Entry);
	if (RequestSize > BufferCap) RequestSize = BufferCap;

	// Complete any previously issued read before re-arming.
	if (Entry.bReadPending)
	{
		DWORD Bytes = 0;
		if (::GetOverlappedResult(Entry.FileHandle, &Entry.ReadOv, &Bytes, FALSE))
		{
			Entry.bReadPending = false;
		}
		else
		{
			const DWORD Err = ::GetLastError();
			if (Err == ERROR_IO_INCOMPLETE)
			{
				return; // no data yet, keep previous buffer contents
			}
			// Device likely gone; drop handle so registry re-detects.
			Impl->CloseEntry_NoLock(Entry);
			Impl->Handles.erase(It);
			Context->Handle = INVALID_PLATFORM_HANDLE;
			Context->IsConnected = false;
			return;
		}
	}

	// Drain any reports that are already queued in the kernel so the parser
	// lands on the newest. HID rate (~250 Hz USB / 1000 Hz BT) is well above
	// the game's frame rate so multiple reports usually sit ready at tick
	// entry; consuming only one per tick adds up to a frame of stale input.
	// Bounded so a burst can't stall the pump.
	constexpr int kMaxDrain = 4;
	for (int i = 0; i < kMaxDrain; ++i)
	{
		::ResetEvent(Entry.ReadEvent);
		std::memset(&Entry.ReadOv, 0, sizeof(Entry.ReadOv));
		Entry.ReadOv.hEvent = Entry.ReadEvent;

		DWORD Bytes = 0;
		const BOOL Ok = ::ReadFile(Entry.FileHandle, Buffer, RequestSize, &Bytes, &Entry.ReadOv);
		if (Ok)
		{
			// Completed synchronously - newer data overwrote Buffer. Try for more.
			Entry.bReadPending = false;
			continue;
		}

		const DWORD Err = ::GetLastError();
		if (Err == ERROR_IO_PENDING)
		{
			// No more reports queued right now; keep this one armed for next tick.
			Entry.bReadPending = true;
			return;
		}

		// Hard failure (device yanked, etc).
		Impl->CloseEntry_NoLock(Entry);
		Impl->Handles.erase(It);
		Context->Handle = INVALID_PLATFORM_HANDLE;
		Context->IsConnected = false;
		return;
	}

	// Drain cap reached. Buffer holds the newest report consumed; next Tick's
	// Read() will arm a fresh overlapped read on entry.
	Entry.bReadPending = false;
}

void FHIDDeviceInfo::Write(FDeviceContext* Context)
{
	if (!Context) return;

	std::lock_guard<std::mutex> Lock(Impl->Mutex);
	auto It = Impl->Handles.find(Context->Path);
	if (It == Impl->Handles.end()) return;
	auto& Entry = It->second;
	if (Entry.FileHandle == INVALID_HANDLE_VALUE) return;

	unsigned char* Out = Context->GetRawOutputBuffer();
	const unsigned long Size = FImpl::GetWriteSize(Context);

	OVERLAPPED Ov{};
	Ov.hEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
	if (Ov.hEvent == nullptr) return;

	DWORD Written = 0;
	const BOOL Ok = ::WriteFile(Entry.FileHandle, Out, Size, &Written, &Ov);
	if (!Ok)
	{
		const DWORD Err = ::GetLastError();
		if (Err == ERROR_IO_PENDING)
		{
			::GetOverlappedResult(Entry.FileHandle, &Ov, &Written, TRUE);
		}
		else
		{
			::CloseHandle(Ov.hEvent);
			Impl->CloseEntry_NoLock(Entry);
			Impl->Handles.erase(It);
			Context->Handle = INVALID_PLATFORM_HANDLE;
			Context->IsConnected = false;
			return;
		}
	}
	::CloseHandle(Ov.hEvent);
}
