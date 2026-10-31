// Probe DJI's bulk transport: the RC-N3 exposes MI_00/MI_01 as libusb-win32
// devices (installed by DJI's own driver package), and that is where DJI Fly
// reads stick data. This discovers the device node, validates the libusb0
// IOCTL numbering, and tries every plausible bulk endpoint pair.

#include <windows.h>
#include <winioctl.h>

#include <cstdio>
#include <string>
#include <vector>

#include "Crc.h"
#include "Duml.h"
#include "Util.h"

namespace dji {
namespace {

constexpr ULONG kFileDeviceUsb = 0x00000022;  // FILE_DEVICE_UNKNOWN

constexpr ULONG CtlCode(ULONG device, ULONG function, ULONG method, ULONG access) {
    return (device << 16) | (access << 14) | (function << 2) | method;
}

// libusb-win32 usb.h request codes.
constexpr ULONG kIoctlRead = CtlCode(kFileDeviceUsb, 0x801, METHOD_BUFFERED, FILE_READ_ACCESS);
constexpr ULONG kIoctlWrite = CtlCode(kFileDeviceUsb, 0x802, METHOD_BUFFERED, FILE_WRITE_ACCESS);
constexpr ULONG kIoctlReset = CtlCode(kFileDeviceUsb, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS);
constexpr ULONG kIoctlGetDriverVersion =
    CtlCode(kFileDeviceUsb, 0x80A, METHOD_BUFFERED, FILE_ANY_ACCESS);
constexpr ULONG kIoctlGetNumberOfConfigurations =
    CtlCode(kFileDeviceUsb, 0x814, METHOD_BUFFERED, FILE_ANY_ACCESS);
constexpr ULONG kIoctlGetCurrentConfiguration =
    CtlCode(kFileDeviceUsb, 0x815, METHOD_BUFFERED, FILE_ANY_ACCESS);

struct BulkPipeIo {
    ULONG pipe;
    ULONG size;
};

std::vector<std::string> candidate_paths() {
    std::vector<std::string> paths;
    // The real names come from the \?? namespace: list them and keep anything a
    // USB user-mode driver published, instead of guessing the format.
    std::vector<wchar_t> buffer(64 * 1024);
    const DWORD needed = QueryDosDeviceW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (needed > 0) {
        const wchar_t* cursor = buffer.data();
        while (*cursor != L'\0') {
            const std::string name = wide_to_utf8(cursor);
            std::string low = name;
            for (char& c : low) {
                if (c >= 'A' && c <= 'Z') {
                    c = static_cast<char>(c - 'A' + 'a');
                }
            }
            if (low.find("libusb") != std::string::npos || low.find("usb") != std::string::npos) {
                std::printf("  命名空间候选: \\\\.\\%s\n", name.c_str());
                if (low.find("libusb") != std::string::npos) {
                    paths.push_back("\\\\.\\" + name);
                }
            }
            cursor += wcslen(cursor) + 1;
        }
    } else {
        std::printf("  QueryDosDevice 失败 GetLastError=%lu，退回猜测命名\n", GetLastError());
    }
    if (paths.empty()) {
        for (int index = 1; index <= 16; ++index) {
            char path[128];
            std::snprintf(path, sizeof(path), "\\\\.\\libusb0-%04d--0x2ca3-0x1020", index);
            paths.emplace_back(path);
        }
    }
    return paths;
}

bool simple_query(HANDLE handle, ULONG code, void* out, size_t out_size, const char* label) {
    DWORD got = 0;
    const BOOL ok = DeviceIoControl(handle, code, nullptr, 0, out, static_cast<DWORD>(out_size),
                                    &got, nullptr);
    const DWORD err = GetLastError();
    std::printf("    %-26s %s (返回 %lu 字节, GetLastError=%lu)\n", label, ok ? "OK" : "失败", got,
                err);
    return ok != FALSE;
}

bool bulk_write(HANDLE handle, ULONG endpoint, const uint8_t* data, size_t len) {
    std::vector<uint8_t> buffer(sizeof(BulkPipeIo) + len);
    auto* header = reinterpret_cast<BulkPipeIo*>(buffer.data());
    header->pipe = endpoint;
    header->size = static_cast<ULONG>(len);
    memcpy(buffer.data() + sizeof(BulkPipeIo), data, len);
    DWORD done = 0;
    const BOOL ok = DeviceIoControl(handle, kIoctlWrite, buffer.data(),
                                    static_cast<DWORD>(buffer.size()), nullptr, 0, &done, nullptr);
    return ok != FALSE;
}

int bulk_read(HANDLE handle, ULONG endpoint, uint8_t* out, size_t cap, int timeout_ms) {
    std::vector<uint8_t> header(sizeof(BulkPipeIo));
    auto* pipe = reinterpret_cast<BulkPipeIo*>(header.data());
    pipe->pipe = endpoint;
    pipe->size = static_cast<ULONG>(cap);
    DWORD done = 0;
    OVERLAPPED overlapped{};
    overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    const BOOL started = DeviceIoControl(handle, kIoctlRead, header.data(),
                                         static_cast<DWORD>(header.size()), out,
                                         static_cast<DWORD>(cap), &done, &overlapped);
    int result = 0;
    if (!started) {
        const DWORD err = GetLastError();
        if (err == ERROR_IO_PENDING) {
            if (WaitForSingleObject(overlapped.hEvent, static_cast<DWORD>(timeout_ms)) ==
                WAIT_OBJECT_0) {
                DWORD transferred = 0;
                if (GetOverlappedResult(handle, &overlapped, &transferred, FALSE)) {
                    result = static_cast<int>(transferred);
                }
            } else {
                CancelIo(handle);
            }
        }
    } else {
        result = static_cast<int>(done);
    }
    CloseHandle(overlapped.hEvent);
    return result;
}

// Maps which request codes the driver actually implements: an unsupported code
// fails with ERROR_INVALID_FUNCTION, a recognised one fails on parameters instead.
void map_ioctls(HANDLE handle) {
    std::string supported;
    char line[96];
    for (ULONG function = 0x800; function <= 0x830; ++function) {
        const ULONG code = CtlCode(kFileDeviceUsb, function, METHOD_BUFFERED, FILE_ANY_ACCESS);
        ULONG scratch = 0;
        DWORD done = 0;
        const BOOL ok = DeviceIoControl(handle, code, nullptr, 0, &scratch, sizeof(scratch), &done,
                                        nullptr);
        const DWORD err = GetLastError();
        if (ok) {
            std::snprintf(line, sizeof(line), "0x%03lX=OK ", function);
        } else if (err != ERROR_INVALID_FUNCTION) {
            std::snprintf(line, sizeof(line), "0x%03lX->%lu ", function, err);
        } else {
            continue;
        }
        supported += line;
    }
    std::printf("    驱动认得的 IOCTL: %s\n", supported.empty() ? "(无)" : supported.c_str());
}

}  // namespace

int probe_bulk(int timeout_ms) {
    std::printf("枚举 libusb0 设备节点 (DJI RC VID_2CA3 PID_1020)...\n");
    std::vector<HANDLE> open_handles;
    for (const auto& path : candidate_paths()) {
        HANDLE handle = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            continue;
        }
        std::printf("  打开 %s\n", path.c_str());
        open_handles.push_back(handle);
    }
    if (open_handles.empty()) {
        std::printf("  没有打开任何 libusb0 节点。确认遥控器插着且 MI_00/MI_01 已绑 libusb0。\n");
        return 1;
    }

    uint16_t sequence = 0x1234;
    int hits = 0;
    for (size_t i = 0; i < open_handles.size(); ++i) {
        HANDLE handle = open_handles[i];
        std::printf("  [节点 %zu] IOCTL 编号自检:\n", i + 1);
        ULONG scratch = 0;
        simple_query(handle, kIoctlGetDriverVersion, &scratch, sizeof(scratch), "GET_DRIVER_VERSION");
        simple_query(handle, kIoctlGetNumberOfConfigurations, &scratch, sizeof(scratch),
                     "GET_NUM_CONFIGURATIONS");
        simple_query(handle, kIoctlGetCurrentConfiguration, &scratch, sizeof(scratch),
                     "GET_CURRENT_CONFIGURATION");
        map_ioctls(handle);

        uint8_t frame[kMaxFrameSize];
        const size_t len = build_request(frame, sizeof(frame), kCmdSimulatorMode,
                                         reinterpret_cast<const uint8_t*>("\x01"), 1, sequence++);
        uint8_t inbox[512];
        for (ULONG out_ep = 0x01; out_ep <= 0x04; ++out_ep) {
            for (ULONG in_ep = 0x81; in_ep <= 0x84; ++in_ep) {
                if (!bulk_write(handle, out_ep, frame, len)) {
                    std::printf("    OUT=0x%02X write failed GetLastError=%lu\n", out_ep,
                                GetLastError());
                    continue;  // endpoint not usable, try the next pair
                }
                const int got = bulk_read(handle, in_ep, inbox, sizeof(inbox), timeout_ms);
                if (got > 0) {
                    ++hits;
                    std::printf("    OUT=0x%02X IN=0x%02X -> %d 字节: ", out_ep, in_ep, got);
                    for (int b = 0; b < got && b < 32; ++b) {
                        std::printf("%02X", inbox[b]);
                    }
                    std::printf("\n");
                }
            }
        }
    }
    for (HANDLE handle : open_handles) {
        CloseHandle(handle);
    }
    if (hits == 0) {
        std::printf("  所有端点组合都没有回数据。\n");
        return 4;
    }
    return 0;
}

}  // namespace dji
