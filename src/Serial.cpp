#include "Serial.h"

#include <windows.h>

#include <initguid.h>
#include <cfgmgr32.h>
#include <devguid.h>
#include <setupapi.h>

#include <algorithm>
#include <cstring>

#include "Log.h"

// DECLARE_INTERFACE_(GUID_DEVINTERFACE_COMPORT, ...) lives in the SDK headers only as
// a comment for some Windows SDK versions, so the value is spelled out here.
DEFINE_GUID(GUID_DEVINTERFACE_COMPORT, 0x86e0e1de, 0x80e5, 0x4fe8, 0x83, 0xd7, 0xfe, 0x22,
            0x80, 0x21, 0x8e, 0x17);

namespace dji {
namespace {

std::string to_utf8(const wchar_t* wide) {
    if (wide == nullptr) {
        return {};
    }
    const int len = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) {
        return {};
    }
    std::string out(static_cast<size_t>(len) - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, out.data(), len, nullptr, nullptr);
    return out;
}

std::string lower_ascii(std::string value) {
    for (char& c : value) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return value;
}

bool contains(const std::string& haystack, const char* needle) {
    return haystack.find(needle) != std::string::npos;
}

bool extract_com_number(const std::string& text, std::string& out) {
    const std::string low = lower_ascii(text);
    const size_t pos = low.find("com");
    if (pos == std::string::npos) {
        return false;
    }
    size_t i = pos + 3;
    std::string digits;
    while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
        digits.push_back(text[i++]);
    }
    if (digits.empty()) {
        return false;
    }
    out = "COM" + digits;
    return true;
}

uint16_t parse_id_after(const std::string& low, const char* marker) {
    const size_t pos = low.find(marker);
    if (pos == std::string::npos) {
        return 0;
    }
    uint32_t value = 0;
    size_t i = pos + std::strlen(marker);
    for (int n = 0; n < 4 && i < low.size(); ++n, ++i) {
        const char c = low[i];
        int digit;
        if (c >= '0' && c <= '9') {
            digit = c - '0';
        } else if (c >= 'a' && c <= 'f') {
            digit = c - 'a' + 10;
        } else {
            break;
        }
        value = value * 16U + static_cast<uint32_t>(digit);
    }
    return static_cast<uint16_t>(value);
}

std::wstring read_reg_string(HKEY key, const wchar_t* name) {
    wchar_t buffer[512];
    DWORD type = 0;
    DWORD size = sizeof(buffer);
    if (RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ, &type, buffer, &size) != ERROR_SUCCESS) {
        return {};
    }
    buffer[(size / sizeof(wchar_t)) - 1 < 511 ? (size / sizeof(wchar_t)) - 1 : 511] = L'\0';
    return std::wstring(buffer);
}

void collect_ports(HDEVINFO set, std::vector<PortInfo>& ports) {
    SP_DEVINFO_DATA info{};
    info.cbSize = sizeof(info);
    for (DWORD index = 0; SetupDiEnumDeviceInfo(set, index, &info); ++index) {
        PortInfo port;

        wchar_t buffer[1024];
        DWORD required = 0;
        DWORD type = 0;
        if (SetupDiGetDeviceRegistryPropertyW(set, &info, SPDRP_FRIENDLYNAME, &type,
                                              reinterpret_cast<BYTE*>(buffer), sizeof(buffer),
                                              &required) &&
            required > 0) {
            port.friendly = to_utf8(buffer);
        }
        if (SetupDiGetDeviceRegistryPropertyW(set, &info, SPDRP_DEVICEDESC, &type,
                                              reinterpret_cast<BYTE*>(buffer), sizeof(buffer),
                                              &required) &&
            required > 0) {
            const std::string desc = to_utf8(buffer);
            if (port.friendly.empty()) {
                port.friendly = desc;
            } else {
                // The DJI interface marker lives in DeviceDesc, not in FriendlyName.
                port.friendly += " " + desc;
            }
        }

        wchar_t hwid[1024] = {};
        if (SetupDiGetDeviceRegistryPropertyW(set, &info, SPDRP_HARDWAREID, &type,
                                              reinterpret_cast<BYTE*>(hwid), sizeof(hwid),
                                              &required) &&
            required > 0) {
            port.hwid = to_utf8(hwid);  // first string of the multi-sz
            const std::string low = lower_ascii(port.hwid);
            port.vid = parse_id_after(low, "vid_");
            port.pid = parse_id_after(low, "pid_");
        }

        if (!extract_com_number(port.friendly, port.com)) {
            // Fall back to the PortName value of the device key (COM10+ devices often
            // carry the number only there).
            const HKEY key = SetupDiOpenDevRegKey(set, &info, DICS_FLAG_GLOBAL, 0, DIREG_DEV,
                                                 KEY_READ);
            if (key != INVALID_HANDLE_VALUE) {
                const std::wstring name = read_reg_string(key, L"PortName");
                RegCloseKey(key);
                if (!name.empty()) {
                    port.com = to_utf8(name.c_str());
                }
            }
        }
        if (port.com.empty()) {
            continue;
        }

        const std::string low = lower_ascii(port.friendly);
        if (contains(low, "debug")) {
            port.score = -1000;
        } else {
            if (contains(low, "for protocol")) {
                port.score += 100;
            }
            if (contains(low, "dji") || contains(low, "vcom")) {
                port.score += 25;
            }
            if (port.vid == 0x2CA3) {
                port.score += 20;
            }
            if (contains(low, "usb")) {
                port.score += 5;
            }
        }
        ports.push_back(port);
    }
}

}  // namespace

std::vector<PortInfo> enumerate_ports() {
    std::vector<PortInfo> ports;
    struct SetGuard {
        HDEVINFO set;
        explicit SetGuard(HDEVINFO s) : set(s) {}
        ~SetGuard() {
            if (set != INVALID_HANDLE_VALUE) {
                SetupDiDestroyDeviceInfoList(set);
            }
        }
        bool valid() const { return set != INVALID_HANDLE_VALUE; }
    };

    // 1) The Ports device class is where "(COMn)" friendly names live.
    const SetGuard class_set(
        SetupDiGetClassDevs(&GUID_DEVCLASS_PORTS, nullptr, nullptr, DIGCF_PRESENT));
    if (class_set.valid()) {
        collect_ports(class_set.set, ports);
    } else {
        LOGW("Ports 类枚举失败 GetLastError=%lu", GetLastError());
    }

    // 2) Some CDC devices only surface through the COMPort device interface.
    const SetGuard iface_set(SetupDiGetClassDevs(&GUID_DEVINTERFACE_COMPORT, nullptr, nullptr,
                                                DIGCF_PRESENT | DIGCF_DEVICEINTERFACE));
    if (iface_set.valid()) {
        collect_ports(iface_set.set, ports);
    }

    std::sort(ports.begin(), ports.end(), [](const PortInfo& a, const PortInfo& b) {
        if (a.score != b.score) {
            return a.score > b.score;
        }
        return a.com < b.com;
    });
    // Drop duplicates: the same COM can be reported by both enumeration paths.
    std::vector<PortInfo> unique;
    for (auto& port : ports) {
        if (std::any_of(unique.begin(), unique.end(),
                        [&](const PortInfo& p) { return p.com == port.com; })) {
            continue;
        }
        unique.push_back(std::move(port));
    }
    return unique;
}

std::vector<UsbAnomaly> enumerate_usb_anomalies() {
    std::vector<UsbAnomaly> out;
    const HDEVINFO set = SetupDiGetClassDevs(&GUID_DEVCLASS_USB, nullptr, nullptr, DIGCF_PRESENT);
    if (set == INVALID_HANDLE_VALUE) {
        return out;
    }
    SP_DEVINFO_DATA info{};
    info.cbSize = sizeof(info);
    for (DWORD index = 0; SetupDiEnumDeviceInfo(set, index, &info); ++index) {
        wchar_t buffer[1024];
        DWORD required = 0;
        DWORD type = 0;
        std::string text;
        if (SetupDiGetDeviceRegistryPropertyW(set, &info, SPDRP_FRIENDLYNAME, &type,
                                              reinterpret_cast<BYTE*>(buffer), sizeof(buffer),
                                              &required) &&
            required > 0) {
            text = to_utf8(buffer);
        }
        if (text.empty() &&
            SetupDiGetDeviceRegistryPropertyW(set, &info, SPDRP_DEVICEDESC, &type,
                                              reinterpret_cast<BYTE*>(buffer), sizeof(buffer),
                                              &required) &&
            required > 0) {
            text = to_utf8(buffer);
        }
        wchar_t dev_id[1024] = {};
        if (SetupDiGetDeviceInstanceIdW(set, &info, dev_id, 1024, &required) == FALSE) {
            continue;
        }
        const std::string instance = to_utf8(dev_id);

        DWORD status = 0;
        DWORD problem = 0;
        if (CM_Get_DevNode_Status(&status, &problem, info.DevInst, 0) != CR_SUCCESS) {
            problem = 0;
        }
        const bool dji_vid = contains(lower_ascii(instance), "2ca3") || contains(lower_ascii(text), "dji");
        if (!dji_vid && problem == 0) {
            continue;  // healthy and not DJI: nothing worth reporting
        }
        out.push_back(UsbAnomaly{instance, text, problem, dji_vid});
    }
    SetupDiDestroyDeviceInfoList(set);
    return out;
}

SerialPort::~SerialPort() { close(); }

bool SerialPort::open(const std::string& com, uint32_t baud, bool dtr, bool rts) {
    close();
    const std::string path = "\\\\.\\" + com;
    // Synchronous handle: read_some() calls ReadFile without an OVERLAPPED, which is
    // invalid on a handle opened with FILE_FLAG_OVERLAPPED (it fails and reports 0
    // bytes, looking exactly like a silent device).
    handle_ = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                          FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle_ == invalid_handle) {
        char msg[256];
        std::snprintf(msg, sizeof(msg), "CreateFileA(%s) GetLastError=%lu", com.c_str(),
                      GetLastError());
        last_error_ = msg;
        return false;
    }

    DCB dcb{};
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(static_cast<HANDLE>(handle_), &dcb)) {
        last_error_ = "GetCommState failed";
        close();
        return false;
    }
    char spec[128];
    std::snprintf(spec, sizeof(spec), "baud=%u parity=N data=8 stop=1", baud);
    if (!BuildCommDCBA(spec, &dcb)) {
        last_error_ = "BuildCommDCBA failed";
        close();
        return false;
    }
    dcb.fBinary = TRUE;
    dcb.fParity = FALSE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl = dtr ? DTR_CONTROL_ENABLE : DTR_CONTROL_DISABLE;
    dcb.fRtsControl = rts ? RTS_CONTROL_ENABLE : RTS_CONTROL_DISABLE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;
    dcb.fAbortOnError = FALSE;
    if (!SetCommState(static_cast<HANDLE>(handle_), &dcb)) {
        last_error_ = "SetCommState failed";
        close();
        return false;
    }

    COMMTIMEOUTS timeouts{};
    // Return from ReadFile straight away with whatever is already buffered.
    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.ReadTotalTimeoutMultiplier = MAXDWORD;
    timeouts.ReadTotalTimeoutConstant = 0;
    timeouts.WriteTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = 250;
    if (!SetCommTimeouts(static_cast<HANDLE>(handle_), &timeouts)) {
        last_error_ = "SetCommTimeouts failed";
        close();
        return false;
    }
    SetupComm(static_cast<HANDLE>(handle_), 8192, 2048);
    purge_all();
    last_error_.clear();
    return true;
}

void SerialPort::close() {
    if (handle_ != invalid_handle) {
        CloseHandle(static_cast<HANDLE>(handle_));
        handle_ = invalid_handle;
    }
}

void SerialPort::set_dtr(bool on) {
    if (is_open()) {
        EscapeCommFunction(static_cast<HANDLE>(handle_), on ? SETDTR : CLRDTR);
    }
}

void SerialPort::set_rts(bool on) {
    if (is_open()) {
        EscapeCommFunction(static_cast<HANDLE>(handle_), on ? SETRTS : CLRRTS);
    }
}

void SerialPort::purge_all() {
    if (is_open()) {
        PurgeComm(static_cast<HANDLE>(handle_), PURGE_RXCLEAR | PURGE_TXCLEAR | PURGE_RXABORT |
                                                     PURGE_TXABORT);
    }
}

size_t SerialPort::in_waiting() {
    if (!is_open()) {
        return 0;
    }
    COMSTAT stat{};
    DWORD errors = 0;
    if (!ClearCommError(static_cast<HANDLE>(handle_), &errors, &stat)) {
        return 0;
    }
    return stat.cbInQue;
}

size_t SerialPort::write_all(const uint8_t* data, size_t len) {
    if (!is_open()) {
        return 0;
    }
    DWORD written = 0;
    if (!WriteFile(static_cast<HANDLE>(handle_), data, static_cast<DWORD>(len), &written,
                   nullptr)) {
        return 0;
    }
    return written;
}

size_t SerialPort::read_some(uint8_t* data, size_t cap) {
    if (!is_open() || cap == 0) {
        return 0;
    }
    DWORD got = 0;
    if (!ReadFile(static_cast<HANDLE>(handle_), data, static_cast<DWORD>(cap), &got, nullptr)) {
        return 0;
    }
    return got;
}

}  // namespace dji
