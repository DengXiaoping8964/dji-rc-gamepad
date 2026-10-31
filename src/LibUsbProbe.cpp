// Drives the RC-N3 bulk transport through libusb-1.0, the same stack DJI's own
// Assistant ships. libusb-1.0 uses opaque handles, so no internal struct layouts
// are guessed here; the DLL is loaded by path so the build stays dependency-free.

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "BulkProbe.h"
#include "Duml.h"
#include "Util.h"

namespace dji {
namespace {

struct LibUsbApi {
    using Init = int(void**);
    using Exit = void(void*);
    using OpenVidPid = void*(void*, unsigned short, unsigned short);
    using Claim = int(void*, int);
    using Release = int(void*, int);
    using SetConfig = int(void*, int);
    using GetConfig = int(void*, int*);
    using Bulk = int(void*, unsigned char, unsigned char*, int, int*, unsigned);
    using Cancel = int(void*);
    using ErrorName = const char*(int);
    using StrError = const char*(int);

    HMODULE module = nullptr;
    Init* init = nullptr;
    Exit* exit = nullptr;
    OpenVidPid* open = nullptr;
    Claim* claim = nullptr;
    Release* release = nullptr;
    SetConfig* set_config = nullptr;
    GetConfig* get_config = nullptr;
    Bulk* bulk = nullptr;
    Cancel* cancel = nullptr;
    ErrorName* error_name = nullptr;
    StrError* str_error = nullptr;

    bool load(std::string& source) {
        static const char* kPaths[] = {
            "libusb-1.0.dll",
            "C:\\Program Files (x86)\\DJI Product\\DJI Assistant 2 (Consumer Drones "
            "Series)\\DJIEngine\\libusb-1.0.dll",
            "D:\\Temp\\djilib\\libusb-1.0.dll",
        };
        for (const char* path : kPaths) {
            module = LoadLibraryExA(path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
            if (module != nullptr) {
                source = path;
                break;
            }
        }
        if (module == nullptr) {
            return false;
        }
        init = reinterpret_cast<Init*>(GetProcAddress(module, "libusb_init"));
        exit = reinterpret_cast<Exit*>(GetProcAddress(module, "libusb_exit"));
        open = reinterpret_cast<OpenVidPid*>(GetProcAddress(module, "libusb_open_device_with_vid_pid"));
        claim = reinterpret_cast<Claim*>(GetProcAddress(module, "libusb_claim_interface"));
        release = reinterpret_cast<Release*>(GetProcAddress(module, "libusb_release_interface"));
        set_config = reinterpret_cast<SetConfig*>(GetProcAddress(module, "libusb_set_configuration"));
        get_config = reinterpret_cast<GetConfig*>(GetProcAddress(module, "libusb_get_configuration"));
        bulk = reinterpret_cast<Bulk*>(GetProcAddress(module, "libusb_bulk_transfer"));
        cancel = reinterpret_cast<Cancel*>(GetProcAddress(module, "libusb_cancel_transfer"));
        error_name = reinterpret_cast<ErrorName*>(GetProcAddress(module, "libusb_error_name"));
        str_error = reinterpret_cast<StrError*>(GetProcAddress(module, "libusb_strerror"));
        return init && exit && open && claim && bulk;
    }

    const char* describe(int code) {
        if (error_name != nullptr) {
            return error_name(code);
        }
        if (str_error != nullptr) {
            return str_error(code);
        }
        return "?";
    }
};

}  // namespace

int probe_libusb(int timeout_ms, unsigned short vendor, unsigned short product) {
    LibUsbApi usb;
    std::string source;
    if (!usb.load(source)) {
        std::printf("  找不到 libusb-1.0.dll (GetLastError=%lu)\n", GetLastError());
        return 1;
    }
    std::printf("  已加载 %s\n", source.c_str());

    void* context = nullptr;
    int code = usb.init(&context);
    if (code != 0) {
        std::printf("  libusb_init 失败: %s\n", usb.describe(code));
        return 2;
    }
    void* handle = usb.open(context, vendor, product);
    if (handle == nullptr) {
        std::printf("  打不到 VID=%04X PID=%04X 的设备（libusb 后端不认这个驱动？）\n", vendor,
                    product);
        usb.exit(context);
        return 3;
    }
    std::printf("  设备已打开 VID_%04X&PID_%04X\n", vendor, product);

    int active = -1;
    if (usb.get_config != nullptr) {
        usb.get_config(handle, &active);
    }
    std::printf("  当前配置 = %d\n", active);
    if (usb.set_config != nullptr && active <= 0) {
        std::printf("  set_configuration(1) -> %s\n", usb.describe(usb.set_config(handle, 1)));
    }

    std::vector<uint8_t> frame(kMaxFrameSize);
    uint16_t seq = 0x1234;
    const uint8_t on = 0x01;
    const size_t enable_len =
        build_request(frame.data(), frame.size(), kCmdSimulatorMode, &on, 1, seq++);

    int hits = 0;
    for (int interface_number = 0; interface_number <= 1; ++interface_number) {
        const int claimed = usb.claim(handle, interface_number);
        std::printf("  claim_interface(%d) -> %s\n", interface_number, usb.describe(claimed));
        if (claimed != 0) {
            continue;
        }
        for (int out_ep = 0x01; out_ep <= 0x04; ++out_ep) {
            int transferred = 0;
            const int wrote = usb.bulk(handle, static_cast<unsigned char>(out_ep), frame.data(),
                                       static_cast<int>(enable_len), &transferred,
                                       static_cast<unsigned>(timeout_ms));
            if (wrote != 0) {
                continue;
            }
            for (int in_ep = 0x81; in_ep <= 0x84; ++in_ep) {
                std::vector<uint8_t> inbox(256);
                int got = 0;
                const uint8_t request[kMinFrameSize] = {0x55, 0x0D, 0x04};
                (void)request;
                const int read = usb.bulk(handle, static_cast<unsigned char>(in_ep), inbox.data(),
                                          static_cast<int>(inbox.size()), &got,
                                          static_cast<unsigned>(timeout_ms));
                if (read == 0 && got > 0) {
                    ++hits;
                    std::printf("    if%d OUT=0x%02X IN=0x%02X -> %d 字节: ", interface_number,
                                out_ep, in_ep, got);
                    for (int b = 0; b < got && b < 32; ++b) {
                        std::printf("%02X", inbox[b]);
                    }
                    std::printf("\n");
                } else if (read != 0 && read != -7 /* TIMEOUT */) {
                    std::printf("    if%d IN=0x%02X -> %s\n", interface_number, in_ep,
                                usb.describe(read));
                }
            }
        }
        if (usb.release != nullptr) {
            usb.release(handle, interface_number);
        }
    }
    usb.exit(context);
    if (hits == 0) {
        std::printf("  libusb 也没读到任何数据。\n");
        return 4;
    }
    return 0;
}

}  // namespace dji
