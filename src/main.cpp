#include <windows.h>
#include <mmsystem.h>

#include <ViGEm/Client.h>

#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "BulkProbe.h"
#include "Config.h"
#include "Crc.h"
#include "Link.h"
#include "Log.h"
#include "Mapping.h"
#include "RcState.h"
#include "Serial.h"
#include "Util.h"

using namespace dji;

namespace {

std::atomic<bool> g_running{true};

BOOL WINAPI control_handler(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_CLOSE_EVENT) {
        g_running = false;
        return TRUE;
    }
    return FALSE;
}

const char* kDefaultProfile = R"INI(
[meta]
name = default

[auto]
process =
title =

[hold]
; 长按多少毫秒算"进入修饰层"。短按仍然是下面 base 段里的按键。
ms = 350

[modifiers]
; L = 长按自定义键, R = 长按急停/返航键
L = btn.fn
R = btn.rth

[pad]
; ---- 基础层：短按 ----
A = btn.rec
X = btn.pv
B = btn.rth
Y = btn.fn
; 三段飞行挡位开关直接当三个肩键用
LB = mode.sport
RB = mode.normal
BACK = mode.cine

; ---- L 层：长按自定义键 ----
START.L = btn.rec
LS.L = btn.pv
RS.L = btn.rth
DPAD_UP.L = stick.ry>0.8
DPAD_DOWN.L = stick.ry<-0.8
DPAD_LEFT.L = stick.rx<-0.8
DPAD_RIGHT.L = stick.rx>0.8

; ---- R 层：长按急停/返航键 ----
A.R = btn.rec
X.R = btn.pv
Y.R = btn.fn
LB.R = mode.sport
RB.R = mode.normal
START.R = mode.cine
DPAD_UP.R = stick.rx>0.85
DPAD_DOWN.R = stick.rx<-0.85
DPAD_LEFT.R = stick.ry<-0.85
DPAD_RIGHT.R = stick.ry>0.85

[axis]
LX = stick.lx
LY = stick.ly
RX = stick.rx
RY = stick.ry
; 单个云台拨轮拆成两个模拟扳机：向前拨 = RT，向后拨 = LT
LT = dial.neg
RT = dial.pos
)INI";

const char* kShooterProfile = R"INI(
[meta]
name = shooter

[auto]
process = bf4.exe bf1.exe bfv.exe bf2042.exe gtav.exe rdr2.exe saintsrow.exe justcause3.exe justcause4.exe
title =

[hold]
ms = 300

[modifiers]
L = btn.fn
R = btn.rth

[pad]
A = btn.rec
B = btn.pv
X = btn.fn
Y = btn.rth
LB = mode.sport
RB = mode.normal
; 射击游戏里 RT 是开火、LT 是瞄准，拨轮一个不够时把 RB 也当备用开火
LT = dial.neg
RT = dial.pos

START.L = btn.pv
BACK.L = btn.rec
LS.L = btn.rth
DPAD_UP.L = stick.rx>0.85
DPAD_DOWN.L = stick.rx<-0.85
DPAD_LEFT.L = stick.ry<-0.85
DPAD_RIGHT.L = stick.ry>0.85

A.R = btn.rec
X.R = btn.pv
Y.R = btn.fn
START.R = mode.cine

[axis]
LX = stick.lx
LY = stick.ly
RX = stick.rx
RY = stick.ry
LT = dial.neg
RT = dial.pos
)INI";

struct Options {
    std::string mode = "run";
    std::string config;
    std::string profile;
    std::string port;
    std::string profiles_dir;
    int seconds = 0;
    bool write_defaults = false;
    bool list = false;
    unsigned long baud = 115200;
    bool dtr = true;
    bool rts = true;
    unsigned long target = 0x06;
    unsigned long source = 0x0A;
    unsigned long type = 0x40;
    unsigned long set = 0x06;
    unsigned long id = 0x01;
    std::string data;
    long probe_from = 0x00;
    long probe_to = 0x40;
};

std::string g_config_path;
std::string g_profiles_dir;

void print_usage() {
    std::printf(
        "dji-rc-bridge — 把大疆 RC-N3 遥控器变成 Windows 虚拟 Xbox360 手柄\n"
        "\n"
        "用法: dji-rc-bridge.exe [模式] [选项]\n"
        "  --run              (默认) 常驻桥接，遥控器 -> 虚拟手柄\n"
        "  --scan             列出 COM 端口并探测哪个是遥控器\n"
        "  --watch            实时打印原始值/按键位/拨轮增量，用于硬件验证\n"
        "  --calibrate        交互式采集每个摇杆的中心与行程，写回配置\n"
        "  --probe            扫描 DUML 命令号，看 RC-N3 还回什么(默认 0x00..0x40)\n"
        "  --raw              原始字节转储：先证明物理层/波特率对不对，再谈协议\n"
        "\n"
        "选项:\n"
        "  --port COM7        指定串口(默认逐个口用 DUML 实测)\n"
        "  --profile name     指定映射档(默认取配置里的 profile)\n"
        "  --config path      指定 bridge.ini\n"
        "  --baud N           配合 --raw 试不同波特率 (默认 115200)\n"
        "  --seconds N        watch/probe/run 的运行时长, 0 = 直到 Ctrl+C\n"
        "  --init             生成默认 bridge.ini 与 profiles/, 然后退出\n"
        "  --help             本帮助\n");
}

bool parse_args(int argc, char** argv, Options& opt) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&](const char* fallback) {
            if (i + 1 < argc) {
                return std::string(argv[++i]);
            }
            return std::string(fallback);
        };
        if (arg == "--run") opt.mode = "run";
        else if (arg == "--scan") opt.mode = "scan";
        else if (arg == "--watch") opt.mode = "watch";
        else if (arg == "--calibrate") opt.mode = "calibrate";
        else if (arg == "--probe") opt.mode = "probe";
        else if (arg == "--raw") opt.mode = "raw";
        else if (arg == "--legacy") opt.mode = "legacy";
        else if (arg == "--bulk") opt.mode = "bulk";
        else if (arg == "--libusb") opt.mode = "libusb";
        else if (arg == "--sweep") opt.mode = "sweep";
        else if (arg == "--baudsweep") opt.mode = "baudsweep";
        else if (arg == "--try") opt.mode = "try";
        else if (arg == "--listen") opt.mode = "listen";
        else if (arg == "--init") opt.write_defaults = true;
        else if (arg == "--port") opt.port = next("COM7");
        else if (arg == "--profile") opt.profile = next("default");
        else if (arg == "--config") opt.config = next("bridge.ini");
        else if (arg == "--baud") {
            long value = 0;
            if (!parse_int(next("115200"), value) || value <= 0) {
                return false;
            }
            opt.baud = static_cast<unsigned long>(value);
        } else if (arg == "--dtr") {
            opt.dtr = next("on") != "off";
        } else if (arg == "--rts") {
            opt.rts = next("on") != "off";
        } else if (arg == "--type") {
            long value = 0;
            if (!parse_int(next("64"), value) || value < 0 || value > 0xFF) {
                return false;
            }
            opt.type = static_cast<unsigned long>(value);
        } else if (arg == "--set") {
            long value = 0;
            if (!parse_int(next("6"), value) || value < 0 || value > 0xFF) {
                return false;
            }
            opt.set = static_cast<unsigned long>(value);
        } else if (arg == "--id") {
            long value = 0;
            if (!parse_int(next("1"), value) || value < 0 || value > 0xFF) {
                return false;
            }
            opt.id = static_cast<unsigned long>(value);
        } else if (arg == "--data") {
            opt.data = next("");
        } else if (arg == "--source") {
            long value = 0;
            if (!parse_int(next("10"), value) || value < 0 || value > 0xFF) {
                return false;
            }
            opt.source = static_cast<unsigned long>(value);
        } else if (arg == "--target") {
            long value = 0;
            if (!parse_int(next("6"), value) || value < 0 || value > 0xFF) {
                return false;
            }
            opt.target = static_cast<unsigned long>(value);
        } else if (arg == "--seconds") {
            long value = 0;
            if (!parse_int(next("0"), value)) {
                std::printf("--seconds 需要整数\n");
                return false;
            }
            opt.seconds = static_cast<int>(value);
        } else if (arg == "--from") {
            long value = 0;
            if (!parse_int(next("0"), value)) return false;
            opt.probe_from = value;
        } else if (arg == "--to") {
            long value = 0;
            if (!parse_int(next("0"), value)) return false;
            opt.probe_to = value;
        } else if (arg == "--help" || arg == "-h" || arg == "/?") {
            print_usage();
            std::exit(0);
        } else {
            std::printf("未知参数: %s (--help 看用法)\n", arg.c_str());
            return false;
        }
    }
    return true;
}

class VirtualPad {
public:
    VirtualPad() = default;
    ~VirtualPad() { stop(); }
    VirtualPad(const VirtualPad&) = delete;
    VirtualPad& operator=(const VirtualPad&) = delete;

    bool start(std::string& error) {
        client_ = vigem_alloc();
        if (client_ == nullptr) {
            error = "vigem_alloc 返回空";
            return false;
        }
        VIGEM_ERROR result = vigem_connect(client_);
        if (result != VIGEM_ERROR_NONE) {
            error = "vigem_connect 失败 0x" + std::to_string(static_cast<unsigned>(result)) +
                    " — 检查 ViGEmBus 驱动是否在运行";
            vigem_free(client_);
            client_ = nullptr;
            return false;
        }
        target_ = vigem_target_x360_alloc();
        if (target_ == nullptr) {
            error = "vigem_target_x360_alloc 返回空";
            stop();
            return false;
        }
        result = vigem_target_add(client_, target_);
        if (result != VIGEM_ERROR_NONE) {
            error = "vigem_target_add 失败 0x" + std::to_string(static_cast<unsigned>(result));
            stop();
            return false;
        }
        user_index_ = 0;
        ULONG index = 0;
        if (vigem_target_x360_get_user_index(client_, target_, &index) == VIGEM_ERROR_NONE) {
            user_index_ = index;
        }
        return true;
    }

    bool submit(const PadOutput& out) {
        if (client_ == nullptr || target_ == nullptr) {
            return false;
        }
        XUSB_REPORT report{};
        report.wButtons = out.buttons;
        report.bLeftTrigger = out.lt;
        report.bRightTrigger = out.rt;
        report.sThumbLX = out.lx;
        report.sThumbLY = out.ly;
        report.sThumbRX = out.rx;
        report.sThumbRY = out.ry;
        if (vigem_target_x360_update(client_, target_, report) != VIGEM_ERROR_NONE) {
            ++failures_;
            return false;
        }
        ++updates_;
        return true;
    }

    void stop() {
        if (client_ != nullptr && target_ != nullptr) {
            XUSB_REPORT zero{};
            vigem_target_x360_update(client_, target_, zero);
            vigem_target_remove(client_, target_);
        }
        if (target_ != nullptr) {
            vigem_target_free(target_);
            target_ = nullptr;
        }
        if (client_ != nullptr) {
            vigem_disconnect(client_);
            vigem_free(client_);
            client_ = nullptr;
        }
    }

    bool attached() const { return target_ != nullptr; }
    uint64_t updates() const { return updates_; }
    uint64_t failures() const { return failures_; }
    unsigned user_index() const { return static_cast<unsigned>(user_index_); }

private:
    PVIGEM_CLIENT client_ = nullptr;
    PVIGEM_TARGET target_ = nullptr;
    ULONG user_index_ = 0;
    uint64_t updates_ = 0;
    uint64_t failures_ = 0;
};

// ---- config / profile bootstrap ----

void prepare_paths(Options& opt) {
    const std::string base = exe_directory();
    if (opt.config.empty()) {
        g_config_path = base + "\\bridge.ini";
    } else if (opt.config.find('\\') == std::string::npos) {
        g_config_path = base + "\\" + opt.config;
    } else {
        g_config_path = opt.config;
    }
    g_profiles_dir = base + "\\profiles";
    CreateDirectoryW(utf8_to_wide(g_profiles_dir).c_str(), nullptr);
}

bool ensure_profile_files(std::string& error) {
    CreateDirectoryW(utf8_to_wide(g_profiles_dir).c_str(), nullptr);
    std::string existing;
    if (read_text_file(g_profiles_dir + "\\default.ini", existing) && !existing.empty()) {
        return true;
    }
    if (!write_text_file(g_profiles_dir + "\\default.ini", kDefaultProfile)) {
        error = "无法写入 profiles\\default.ini";
        return false;
    }
    if (!write_text_file(g_profiles_dir + "\\shooter.ini", kShooterProfile)) {
        error = "无法写入 profiles\\shooter.ini";
        return false;
    }
    return true;
}

bool load_or_create_config(GlobalConfig& cfg, std::string& error) {
    std::string text;
    if (read_text_file(g_config_path, text)) {
        return load_global_config(g_config_path, cfg, error);
    }
    cfg = GlobalConfig{};
    if (!save_global_config(g_config_path, cfg, error)) {
        return false;
    }
    LOGI("已生成默认配置 %s", g_config_path.c_str());
    return true;
}

const dji::Profile* resolve_profile(const std::string& name,
                                    std::map<std::string, dji::Profile>& cache,
                                    std::string& error) {
    const auto cached = cache.find(name);
    if (cached != cache.end()) {
        return &cached->second;
    }
    dji::Profile profile;
    const std::string path = g_profiles_dir + "\\" + name + ".ini";
    if (!load_profile(path, profile, error)) {
        return nullptr;
    }
    LOGI("已加载映射档 %s (%s): 数字绑定 %zu, 模拟绑定 %zu, 修饰键 %zu, 长按 %dms", profile.name.c_str(),
         path.c_str(), profile.digital.size(), profile.analog.size(), profile.modifiers.size(),
         profile.hold_ms);
    return &cache.emplace(name, std::move(profile)).first->second;
}

std::string match_auto_profile(const std::vector<std::string>& names,
                               const std::map<std::string, dji::Profile>& cache,
                               const std::string& process, const std::string& title) {
    for (const auto& name : names) {
        const auto it = cache.find(name);
        if (it == cache.end() || it->second.match_process.empty()) {
            continue;
        }
        std::string patterns = it->second.match_process;
        size_t pos = 0;
        while (pos < patterns.size()) {
            size_t space = patterns.find(' ', pos);
            if (space == std::string::npos) {
                space = patterns.size();
            }
            std::string token = patterns.substr(pos, space - pos);
            pos = space + 1;
            if (!token.empty() && process.find(token) != std::string::npos) {
                return name;
            }
        }
    }
    for (const auto& name : names) {
        const auto it = cache.find(name);
        if (it == cache.end() || it->second.match_title.empty()) {
            continue;
        }
        if (title.find(it->second.match_title) != std::string::npos) {
            return name;
        }
    }
    return {};
}

// ---- modes ----

int mode_scan(const Options& opt) {
    const auto ports = dji::enumerate_ports();
    if (ports.empty()) {
        std::printf("没有发现任何 COM 端口。请给遥控器开机并用 USB-C 连接本机。\n");
        return 1;
    }
    std::printf("%-7s %-6s %-11s %s\n", "PORT", "SCORE", "VID:PID", "描述");
    for (const auto& port : ports) {
        char ids[32];
        std::snprintf(ids, sizeof(ids), "%04X:%04X", port.vid, port.pid);
        std::printf("%-7s %-6d %-11s %s\n", port.com.c_str(), port.score, ids,
                    port.friendly.c_str());
    }

    const bool any_dji_serial = std::any_of(ports.begin(), ports.end(),
                                            [](const dji::PortInfo& p) { return p.score > 0; });
    std::printf("\n逐个端口实测 DUML 应答...\n");
    const std::string found = dji::RcLink::detect_port(700, opt.port);
    if (found.empty()) {
        if (any_dji_serial) {
            std::printf("  DJI 串口在（%s），但没有一个应答通道包。用 --probe 看它接受哪些命令号。\n",
                        ports.empty() ? "?" : ports[0].com.c_str());
        } else {
            const auto anomalies = dji::enumerate_usb_anomalies();
            if (anomalies.empty()) {
                std::printf("  USB 总线上看不到遥控器。检查：遥控器是否开机、线是否插到底。\n");
            } else {
                std::printf("  没有可用串口，但 USB 总线上有相关节点：\n");
                for (const auto& anomaly : anomalies) {
                    std::printf("    problem=0x%02X %-9s %s\n", anomaly.problem,
                                anomaly.dji_vid ? "DJI" : "失败", anomaly.description.c_str());
                    std::printf("             %s\n", anomaly.instance.c_str());
                }
                std::printf("  注意：遥控器必须插在**充电/调参 USB-C 口**（接手机那个口是它当主机用的）。\n");
            }
        }
        return 2;
    }
    std::printf("  遥控器协议端口: %s\n\n探测响应:\n", found.c_str());

    std::printf("\n探测遥控器响应:\n");
    dji::RcLink link;
    std::string error;
    dji::RcLink::Schedule schedule;
    schedule.channel_hz = 50;
    if (!link.connect(found, schedule, error)) {
        std::printf("  连接失败: %s\n", error.c_str());
        return 3;
    }
    dji::RawState state;
    int channels = 0;
    int buttons = 0;
    const double until = dji::now_seconds() + 2.0;
    while (dji::now_seconds() < until && g_running) {
        if (!link.step(state)) break;
        channels = static_cast<int>(link.stats().channel_frames);
        buttons = static_cast<int>(link.stats().button_frames);
        dji::sleep_ms(2);
    }
    std::printf("  端口 %s: 通道包 %d 个, 按键包 %d 个, 电量 %d%%\n", link.port().c_str(), channels,
                buttons, state.battery);
    if (channels == 0) {
        std::printf("  遥控器没有回应通道包 — 尝试 --probe 看它接受哪些命令。\n");
    }
    return channels > 0 ? 0 : 4;
}

int mode_watch(const Options& opt, GlobalConfig& cfg) {
    dji::RcLink link;
    std::string error;
    if (!link.connect(opt.port.empty() ? cfg.port : opt.port, cfg.schedule, error)) {
        LOGE("连接失败: %s", error.c_str());
        return 1;
    }
    dji::Mapper mapper;
    std::map<std::string, dji::Profile> cache;
    const std::string name = opt.profile.empty() ? cfg.profile : opt.profile;
    const dji::Profile* profile = resolve_profile(name, cache, error);
    if (profile == nullptr) {
        LOGE("映射档加载失败: %s", error.c_str());
        return 1;
    }
    mapper.configure(*profile, cfg);

    std::printf("watch: Ctrl+C 结束。逐行显示 原始值 / 归一化 / 虚拟手柄输出。\n");
    dji::RawState state;
    double last_print = 0.0;
    double started = dji::now_seconds();
    uint8_t last_bits = 0;
    uint8_t last_mode = 0xFF;
    int last_dial = 1024;
    int events = 0;
    while (g_running) {
        const double now = dji::now_seconds();
        if (opt.seconds > 0 && now - started > opt.seconds) {
            break;
        }
        if (!link.step(state)) {
            LOGE("链路中断");
            return 2;
        }
        if (state.button_bits != last_bits) {
            std::printf("  [%8.3f] 按键位变化 0x%02X -> 0x%02X  (fn=%d pv=%d rec=%d rth=%d)\n", now,
                        last_bits, state.button_bits, (state.button_bits & dji::kBitFn) ? 1 : 0,
                        (state.button_bits & dji::kBitPhotoVideo) ? 1 : 0,
                        (state.button_bits & dji::kBitRecord) ? 1 : 0,
                        (state.button_bits & dji::kBitRth) ? 1 : 0);
            last_bits = state.button_bits;
            ++events;
        }
        if (state.flight_mode != last_mode) {
            std::printf("  [%8.3f] 挡位开关 0x%02X -> 0x%02X\n", now, last_mode, state.flight_mode);
            last_mode = state.flight_mode;
            ++events;
        }
        const int dial = state.raw[dji::kAnaDial];
        if (std::abs(dial - last_dial) > 3) {
            std::printf("  [%8.3f] 拨轮 raw %d -> %d (delta %+d)\n", now, last_dial, dial,
                        dial - last_dial);
            last_dial = dial;
            ++events;
        }
        if (now - last_print >= 0.2) {
            last_print = now;
            std::printf(
                "raw  lx=%4d ly=%4d rx=%4d ry=%4d dial=%4d | batt %3d%% | hz %5.1f rtt %4.1fms "
                "bad %llu\n",
                state.raw[dji::kAnaLeftX], state.raw[dji::kAnaLeftY], state.raw[dji::kAnaRightX],
                state.raw[dji::kAnaRightY], dial, state.battery, link.stats().measured_hz,
                link.stats().avg_rtt_ms, static_cast<unsigned long long>(link.stats().frames_bad));
            std::printf("     %s\n", mapper.explain(state, now).c_str());
        }
        dji::sleep_ms(1);
    }
    std::printf("watch 结束: 事件 %d 次, 通道帧 %llu, 按键帧 %llu, 坏帧 %llu\n", events,
                static_cast<unsigned long long>(link.stats().channel_frames),
                static_cast<unsigned long long>(link.stats().button_frames),
                static_cast<unsigned long long>(link.stats().frames_bad));
    return 0;
}

int mode_calibrate(const Options& opt, GlobalConfig& cfg) {
    dji::RcLink link;
    std::string error;
    dji::RcLink::Schedule schedule = cfg.schedule;
    schedule.channel_hz = 100;
    schedule.button_hz = 10;
    if (!link.connect(opt.port.empty() ? cfg.port : opt.port, schedule, error)) {
        LOGE("连接失败: %s", error.c_str());
        return 1;
    }

    dji::RawState state;
    auto pump_for = [&](double seconds) {
        const double until = dji::now_seconds() + seconds;
        while (dji::now_seconds() < until && g_running) {
            if (!link.step(state)) {
                return false;
            }
            dji::sleep_ms(1);
        }
        return true;
    };

    std::printf("校准第 1 步：请把两个摇杆松开、拨轮不要碰，2 秒后自动采集中心…\n");
    if (!pump_for(2.0)) return 2;
    std::vector<std::vector<int>> centers(dji::kAnaCount);
    {
        const double until = dji::now_seconds() + 2.0;
        while (dji::now_seconds() < until && g_running) {
            if (!link.step(state)) return 2;
            for (int i = 0; i < dji::kAnaCount; ++i) {
                centers[i].push_back(state.raw[i]);
            }
            dji::sleep_ms(1);
        }
    }

    std::printf("校准第 2 步：12 秒内把每个摇杆缓慢推到四个角并回中，来回拨动拨轮…\n");
    std::vector<int> minimum(dji::kAnaCount, 1 << 20);
    std::vector<int> maximum(dji::kAnaCount, -(1 << 20));
    {
        const double until = dji::now_seconds() + 12.0;
        while (dji::now_seconds() < until && g_running) {
            if (!link.step(state)) return 2;
            for (int i = 0; i < dji::kAnaCount; ++i) {
                minimum[i] = std::min(minimum[i], state.raw[i]);
                maximum[i] = std::max(maximum[i], state.raw[i]);
            }
            dji::sleep_ms(1);
        }
    }

    bool ok = true;
    for (int i = 0; i < dji::kAnaCount; ++i) {
        auto values = centers[i];
        if (values.empty()) {
            LOGE("%s 没有采到中心样本", dji::kAnalogNames[i]);
            ok = false;
            continue;
        }
        std::sort(values.begin(), values.end());
        const int center = values[values.size() / 2];
        const int lo = std::min(minimum[i], center);
        const int hi = std::max(maximum[i], center);
        if (i < dji::kAnaDial && (center - lo < 60 || hi - center < 60)) {
            LOGW("%s 行程过小 (lo=%d center=%d hi=%d)，该轴可能没推到底", dji::kAnalogNames[i], lo,
                 center, hi);
        }
        AxisCal& cal = cfg.axes[i];
        cal.center = center;
        cal.minimum = lo;
        cal.maximum = hi;
        std::printf("  %-8s min=%5d center=%5d max=%5d\n", dji::kAnalogNames[i], lo, center, hi);
    }
    if (!ok) {
        return 3;
    }
    if (!save_global_config(g_config_path, cfg, error)) {
        LOGE("写回配置失败: %s", error.c_str());
        return 4;
    }
    std::printf("已写入 %s\n", g_config_path.c_str());
    return 0;
}

int mode_probe(const Options& opt, GlobalConfig& cfg) {
    dji::RcLink link;
    std::string error;
    dji::RcLink::Schedule schedule = cfg.schedule;
    if (!link.connect(opt.port.empty() ? cfg.port : opt.port, schedule, error)) {
        LOGE("连接失败: %s", error.c_str());
        return 1;
    }
    std::printf("扫描命令号 0x%02X..0x%02X (每个等待 150ms)\n", static_cast<unsigned>(opt.probe_from),
                static_cast<unsigned>(opt.probe_to));
    for (long id = opt.probe_from; id <= opt.probe_to && g_running; ++id) {
        const auto frames = link.probe(static_cast<uint8_t>(id), nullptr, 0, 150);
        if (frames.empty()) {
            continue;
        }
        for (const auto& frame : frames) {
            std::string hex;
            char byte[8];
            for (size_t i = 0; i < frame.size && i < 24; ++i) {
                std::snprintf(byte, sizeof(byte), "%02X ", frame.data[i]);
                hex += byte;
            }
            std::printf("  cmd 0x%02X -> 长度 %3zu  set=0x%02X  %s\n", id, frame.size,
                        frame.cmd_set(), hex.c_str());
        }
    }
    return 0;
}

std::string to_hex(const uint8_t* data, size_t len) {
    std::string out;
    char byte[4];
    for (size_t i = 0; i < len; ++i) {
        std::snprintf(byte, sizeof(byte), "%02X", data[i]);
        out += byte;
    }
    return out;
}

// Dumps raw bytes so a device that differs from the RC-N1 assumptions can still be
// understood: is there traffic at all, and what does its framing look like?
int mode_raw(const Options& opt) {
    std::string port = opt.port;
    if (port.empty()) {
        const auto ports = dji::enumerate_ports();
        for (const auto& candidate : ports) {
            if (candidate.score > 0) {
                port = candidate.com;
                break;
            }
        }
    }
    if (port.empty()) {
        std::printf("没有 DJI 串口可试。--scan 看端口。\n");
        return 1;
    }
    SerialPort serial;
    if (!serial.open(port, opt.baud, opt.dtr, opt.rts)) {
        std::printf("打开 %s 失败: %s\n", port.c_str(), serial.last_error().c_str());
        return 2;
    }
    std::printf("端口 %s @%lu 8N1 已打开。DTR=%d RTS=%d src=0x%02X dst=0x%02X\n", port.c_str(),
                opt.baud, opt.dtr ? 1 : 0, opt.rts ? 1 : 0, static_cast<unsigned>(opt.source),
                static_cast<unsigned>(opt.target));
    serial.purge_all();

    uint8_t buffer[4096];
    uint64_t total = 0;
    auto listen = [&](const char* label, int ms) {
        uint64_t got = 0;
        char ascii[17] = {};
        const double until = dji::now_seconds() + ms / 1000.0;
        std::printf("  %-22s ", label);
        while (dji::now_seconds() < until) {
            const size_t n = serial.read_some(buffer, sizeof(buffer));
            if (n == 0) {
                dji::sleep_ms(1);
                continue;
            }
            for (size_t i = 0; i < n; ++i) {
                if (got < 48) {
                    std::printf("%02X", buffer[i]);
                    if ((got + 1) % 4 == 0) {
                        std::printf(" ");
                    }
                    ascii[got % 16] = (buffer[i] >= 32 && buffer[i] < 127)
                                          ? static_cast<char>(buffer[i])
                                          : '.';
                    if (got % 16 == 15) {
                        ascii[16] = '\0';
                        std::printf("\n  %-22s ", "");
                        ascii[0] = '\0';
                    }
                }
                ++got;
            }
        }
        if (got == 0) {
            std::printf("(无数据)");
        } else if (got > 48) {
            std::printf("...");
        }
        std::printf("\n      -> %llu 字节\n", static_cast<unsigned long long>(got));
        total += got;
        return got;
    };

    listen("静默接收 1.2s", 1200);
    uint16_t seq = 0x1234;
    uint8_t frame[kMaxFrameSize];
    const uint8_t on = 0x01;
    struct Attempt {
        const char* label;
        uint8_t cmd;
        const uint8_t* payload;
        size_t payload_len;
    };
    const Attempt attempts[] = {
        {"发 0x24 开模拟器", kCmdSimulatorMode, &on, 1},
        {"发 0x01 读通道", kCmdChannels, nullptr, 0},
        {"发 0x27 读按键", kCmdButtons, nullptr, 0},
        {"发 0x1E 读状态", kCmdStatus, nullptr, 0},
    };
    for (const auto& attempt : attempts) {
        const size_t len = build_request(frame, sizeof(frame), attempt.cmd, attempt.payload,
                                        attempt.payload_len, seq++,
                                        static_cast<uint8_t>(opt.source), static_cast<uint8_t>(opt.target));
        const size_t written = serial.write_all(frame, len);
        std::printf("  TX %s: %s (%zu/%zu 字节)\n", attempt.label,
                    to_hex(frame, std::min<size_t>(len, 14)).c_str(), written, len);
        listen(attempt.label, 600);
    }
    std::printf("总计收到 %llu 字节。0 字节 = 波特率或物理层不对；有字节但格式不是 "
                "55 len crc8 = 该机型用另一种帧格式。\n",
                static_cast<unsigned long long>(total));
    return 0;
}

// Last protocol hypothesis: the interface speaks DJI's legacy 0xCC 0x77 framing
// instead of the 0x55 packet used by RC-N1. A device that cannot parse our frame
// stays silent, which looks identical to a dead port.
int mode_legacy(const Options& opt) {
    std::string port = opt.port;
    if (port.empty()) {
        for (const auto& candidate : dji::enumerate_ports()) {
            if (candidate.score > 0) {
                port = candidate.com;
                break;
            }
        }
    }
    if (port.empty()) {
        std::printf("没有 DJI 串口。\n");
        return 1;
    }
    SerialPort serial;
    if (!serial.open(port, opt.baud, opt.dtr, opt.rts)) {
        std::printf("打开 %s 失败: %s\n", port.c_str(), serial.last_error().c_str());
        return 2;
    }
    std::printf("端口 %s: 用 0xCC 0x77 旧帧扫描 category/command\n", port.c_str());
    serial.purge_all();

    uint8_t frame[64];
    uint8_t inbox[512];
    uint64_t any = 0;
    for (int category = 0; category <= 0x1F && g_running; ++category) {
        for (int command = 0; command <= 0x03; ++command) {
            const size_t data_len = 0;
            const size_t total = 6 + data_len + 2;
            frame[0] = 0xCC;
            frame[1] = 0x77;
            frame[2] = static_cast<uint8_t>((total - 6) >> 8);
            frame[3] = static_cast<uint8_t>((total - 6) & 0xFF);
            frame[4] = static_cast<uint8_t>(category);
            frame[5] = static_cast<uint8_t>(command);
            frame[6] = static_cast<uint8_t>(opt.source);
            frame[7] = static_cast<uint8_t>(opt.target);
            const uint16_t crc = crc16_dji(frame, total - 2);
            frame[total - 2] = static_cast<uint8_t>(crc & 0xFF);
            frame[total - 1] = static_cast<uint8_t>(crc >> 8);
            if (serial.write_all(frame, total) != total) {
                std::printf("  写入失败，端口可能已断开\n");
                return 3;
            }
            const double until = dji::now_seconds() + 0.05;
            size_t got = 0;
            while (dji::now_seconds() < until) {
                got += serial.read_some(inbox + got, sizeof(inbox) - got);
                if (got > 0) {
                    break;
                }
            }
            if (got > 0) {
                ++any;
                std::printf("  cat=0x%02X cmd=0x%02X -> %zu 字节: %s\n", category, command, got,
                            to_hex(inbox, std::min<size_t>(got, 24)).c_str());
            }
        }
    }
    if (any == 0) {
        std::printf("  旧帧也无任何回应：这个 CDC 接口对 PC 完全静默。\n");
    }
    return any == 0 ? 4 : 0;
}

// The RC-N3 ignored every RC-N1 command id. What has not been varied yet is the
// command type / command set pair, so sweep them and report ANY raw bytes back.
int mode_sweep(const Options& opt) {
    std::string port = opt.port;
    if (port.empty()) {
        for (const auto& candidate : dji::enumerate_ports()) {
            if (candidate.score >= 100) {
                port = candidate.com;
                break;
            }
        }
    }
    if (port.empty()) {
        std::printf("没有 For Protocol 端口。\n");
        return 1;
    }
    SerialPort serial;
    if (!serial.open(port, opt.baud, opt.dtr, opt.rts)) {
        std::printf("打开 %s 失败: %s\n", port.c_str(), serial.last_error().c_str());
        return 2;
    }
    std::printf("在 %s 上扫描 cmd_type x cmd_set x cmd_id（原始字节接收）\n", port.c_str());
    serial.purge_all();

    const uint8_t types[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80};
    const uint8_t ids[] = {kCmdChannels, kCmdButtons, kCmdStatus, kCmdSimulatorMode};
    const uint8_t enable = 0x01;
    uint8_t frame[kMaxFrameSize];
    uint8_t inbox[512];
    uint16_t seq = 0xBEEF;
    int hits = 0;
    int probes = 0;
    for (const uint8_t type : types) {
        for (int set = 0; set <= 0x0A; ++set) {
            for (const uint8_t id : ids) {
                if (!g_running) {
                    return 3;
                }
                const bool with_payload = (id == kCmdSimulatorMode);
                const size_t len =
                    build_request(frame, sizeof(frame), id, with_payload ? &enable : nullptr,
                                  with_payload ? 1 : 0, seq++, kAddrHost, kAddrRemote);
                // Patch the type/set the shared builder hard-codes.
                frame[8] = type;
                frame[9] = static_cast<uint8_t>(set);
                const uint16_t crc = crc16_dji(frame, len - 2);
                frame[len - 2] = static_cast<uint8_t>(crc & 0xFF);
                frame[len - 1] = static_cast<uint8_t>(crc >> 8);
                if (serial.write_all(frame, len) != len) {
                    std::printf("  写入失败，中止\n");
                    return 4;
                }
                ++probes;
                const double until = dji::now_seconds() + 0.045;
                size_t got = 0;
                while (dji::now_seconds() < until) {
                    const size_t more = serial.read_some(inbox + got, sizeof(inbox) - got);
                    if (more == 0) {
                        dji::sleep_ms(1);
                        continue;
                    }
                    got += more;
                    break;
                }
                if (got > 0) {
                    ++hits;
                    std::printf("  type=0x%02X set=0x%02X id=0x%02X -> %zu 字节: %s\n", type, set,
                                id, got, to_hex(inbox, std::min<size_t>(got, 28)).c_str());
                }
            }
        }
    }
    std::printf("共探测 %d 组合，%d 次有回应。\n", probes, hits);
    return hits == 0 ? 5 : 0;
}

// The RC-N3's VCOM is an internal UART bridge, so a wrong baud rate looks exactly
// like a dead port: writes succeed, nothing answers. Sweep every rate a DJI device
// has plausibly used before reaching for a USB sniffer.
int mode_baud_sweep(const Options& opt) {
    std::string port = opt.port;
    if (port.empty()) {
        for (const auto& candidate : dji::enumerate_ports()) {
            if (candidate.score >= 100) {
                port = candidate.com;
                break;
            }
        }
    }
    if (port.empty()) {
        std::printf("没有 For Protocol 端口。\n");
        return 1;
    }
    const unsigned long kRates[] = {9600,   14400,   19200,   28800,   31250,   38400,
                                    56000,  57600,   64000,   76800,   115200,  128000,
                                    153600, 230400,  250000,  400000,  460800,  500000,
                                    576000, 921600,  1000000, 1152000, 1500000, 2000000,
                                    2500000, 3000000, 3500000};
    std::printf("在 %s 上扫描 %zu 个波特率\n", port.c_str(), std::size(kRates));
    int hits = 0;
    Options probe = opt;
    probe.port = port;
    for (const unsigned long rate : kRates) {
        if (!g_running) {
            break;
        }
        probe.baud = rate;
        SerialPort serial;
        if (!serial.open(port, rate, probe.dtr, probe.rts)) {
            std::printf("  %-8lu 打开失败 (%s)\n", rate, serial.last_error().c_str());
            continue;
        }
        serial.purge_all();
        uint8_t frame[kMaxFrameSize];
        uint16_t seq = 0x5A5A;
        const uint8_t on = 0x01;
        size_t sent = 0;
        sent += serial.write_all(frame, build_request(frame, sizeof(frame), kCmdSimulatorMode, &on, 1,
                                                      seq++, kAddrHost, kAddrRemote));
        sent += serial.write_all(frame, build_request(frame, sizeof(frame), kCmdChannels, nullptr, 0,
                                                      seq++, kAddrHost, kAddrRemote));
        uint8_t inbox[512];
        size_t got = 0;
        const double until = dji::now_seconds() + 0.4;
        while (dji::now_seconds() < until) {
            const size_t more = serial.read_some(inbox + got, sizeof(inbox) - got);
            if (more > 0) {
                got += more;
                if (got >= 8) {
                    break;
                }
                continue;
            }
            dji::sleep_ms(1);
        }
        std::printf("  %-8lu 发送 %zu 字节 -> 收到 %zu 字节%s", rate, sent, got,
                    got > 0 ? "  <== 有回应!\n" : "\n");
        if (got > 0) {
            ++hits;
            std::printf("      %s\n", to_hex(inbox, std::min<size_t>(got, 32)).c_str());
        }
    }
    std::printf("扫描结束：%d 个波特率有回应。\n", hits);
    return hits == 0 ? 5 : 0;
}

// Sends one hand-built DUML request (any type/set/id/payload) and dumps the reply.
// Captured Assistant traffic showed RC-N3 uses set=0x03 id=0xDF where RC-N1 used
// set=0x06 id=0x01, so the command group has to be addressable from the CLI.
int mode_try(const Options& opt) {
    std::string port = opt.port;
    if (port.empty()) {
        for (const auto& candidate : dji::enumerate_ports()) {
            if (candidate.score >= 100) {
                port = candidate.com;
                break;
            }
        }
    }
    if (port.empty()) {
        std::printf("没有 For Protocol 端口。\n");
        return 1;
    }
    SerialPort serial;
    if (!serial.open(port, opt.baud, opt.dtr, opt.rts)) {
        std::printf("打开 %s 失败: %s\n", port.c_str(), serial.last_error().c_str());
        return 2;
    }
    std::vector<uint8_t> payload;
    for (size_t i = 0; i + 1 < opt.data.size(); i += 2) {
        payload.push_back(static_cast<uint8_t>(std::stoul(opt.data.substr(i, 2), nullptr, 16)));
    }
    uint8_t frame[kMaxFrameSize];
    uint16_t seq = 0x1234;
    const size_t len = build_request(frame, sizeof(frame), static_cast<uint8_t>(opt.id),
                                    payload.empty() ? nullptr : payload.data(), payload.size(),
                                    seq++, kAddrHost, static_cast<uint8_t>(opt.target));
    frame[8] = static_cast<uint8_t>(opt.type);
    frame[9] = static_cast<uint8_t>(opt.set);
    const uint16_t crc = crc16_dji(frame, len - 2);
    frame[len - 2] = static_cast<uint8_t>(crc & 0xFF);
    frame[len - 1] = static_cast<uint8_t>(crc >> 8);

    std::printf("发 type=0x%02X set=0x%02X id=0x%02X dst=0x%02X data=%s\n", opt.type, opt.set,
                opt.id, opt.target, to_hex(frame, len).c_str());
    serial.purge_all();
    if (serial.write_all(frame, len) != len) {
        std::printf("写入失败\n");
        return 3;
    }
    uint8_t inbox[1024];
    size_t got = 0;
    const double until = dji::now_seconds() + 0.6;
    while (dji::now_seconds() < until) {
        const size_t more = serial.read_some(inbox + got, sizeof(inbox) - got);
        if (more > 0) {
            got += more;
        } else {
            dji::sleep_ms(1);
        }
    }
    std::printf("收到 %zu 字节\n", got);
    if (got == 0) {
        return 4;
    }
    std::printf("  %s\n", to_hex(inbox, std::min<size_t>(got, 96)).c_str());
    FrameDecoder decoder;
    decoder.feed(inbox, got);
    Frame f;
    while (decoder.pop(f)) {
        std::printf("  -> 解析: len=%zu src=0x%02X dst=0x%02X type=0x%02X set=0x%02X id=0x%02X\n",
                    f.size, f.source(), f.target(), f.cmd_type(), f.cmd_set(), f.cmd_id());
        if (f.cmd_id() == kCmdChannels && f.size >= kChannelReplySize) {
            std::printf("     rx=%u ry=%u ly=%u lx=%u dial=%u\n", f.u16(kOffRightX),
                        f.u16(kOffRightY), f.u16(kOffLeftY), f.u16(kOffLeftX), f.u16(kOffDial));
        }
    }
    std::printf("  坏帧 %llu\n", static_cast<unsigned long long>(decoder.frames_bad));
    return 0;
}

// Holds the port open (DTR asserted, nothing written) and listens. DJI Assistant
// keeps COM7 open for a whole session, so a short open/send/close cycle may never
// catch a device that only starts reporting after the line has been up a while.
int mode_listen(const Options& opt) {
    std::string port = opt.port;
    if (port.empty()) {
        for (const auto& candidate : dji::enumerate_ports()) {
            if (candidate.score >= 100) {
                port = candidate.com;
                break;
            }
        }
    }
    if (port.empty()) {
        std::printf("没有 For Protocol 端口。\n");
        return 1;
    }
    SerialPort serial;
    if (!serial.open(port, opt.baud, opt.dtr, opt.rts)) {
        std::printf("打开 %s 失败: %s\n", port.c_str(), serial.last_error().c_str());
        return 2;
    }
    const int seconds = opt.seconds > 0 ? opt.seconds : 12;
    std::printf("监听 %s %d 秒（不发送任何字节，DTR=%d RTS=%d）\n", port.c_str(), seconds,
                opt.dtr ? 1 : 0, opt.rts ? 1 : 0);
    uint8_t inbox[1024];
    size_t total = 0;
    FrameDecoder decoder;
    Frame f;
    const double until = dji::now_seconds() + seconds;
    while (dji::now_seconds() < until && g_running) {
        const size_t got = serial.read_some(inbox, sizeof(inbox));
        if (got > 0) {
            total += got;
            std::printf("  +%zu 字节: %s\n", got, to_hex(inbox, std::min<size_t>(got, 40)).c_str());
            decoder.feed(inbox, got);
            while (decoder.pop(f)) {
                std::printf("     帧 len=%zu src=0x%02X dst=0x%02X type=0x%02X set=0x%02X id=0x%02X\n",
                            f.size, f.source(), f.target(), f.cmd_type(), f.cmd_set(), f.cmd_id());
            }
        } else {
            dji::sleep_ms(5);
        }
    }
    std::printf("共收到 %zu 字节，解析出 %llu 帧（坏帧 %llu）\n", total,
                static_cast<unsigned long long>(decoder.frames_ok),
                static_cast<unsigned long long>(decoder.frames_bad));
    return total > 0 ? 0 : 4;
}

int mode_run(const Options& opt, GlobalConfig& cfg) {
    std::string error;
    VirtualPad pad;
    if (!pad.start(error)) {
        LOGE("虚拟手柄创建失败: %s", error.c_str());
        return 1;
    }
    LOGI("虚拟 Xbox360 手柄已就绪 (user=%u)。现在打开的游戏会把它当普通手柄。", pad.user_index());

    bool axes_valid = true;
    for (int i = 0; i < dji::kAnaCount; ++i) {
        if (!cfg.axes[i].valid()) {
            axes_valid = false;
        }
    }
    if (!axes_valid) {
        LOGW("摇杆校准看起来还是默认值，建议先跑一次 --calibrate");
    }

    std::map<std::string, dji::Profile> cache;
    std::vector<std::string> profile_names;
    if (cfg.auto_profile) {
        profile_names = dji::list_profiles(g_profiles_dir);
        for (const auto& name : profile_names) {
            std::string load_error;
            resolve_profile(name, cache, load_error);
        }
    }
    const std::string base_name = opt.profile.empty() ? cfg.profile : opt.profile;
    const dji::Profile* active = resolve_profile(base_name, cache, error);
    if (active == nullptr) {
        LOGE("映射档 %s 加载失败: %s", base_name.c_str(), error.c_str());
        return 2;
    }

    dji::Mapper mapper;
    mapper.configure(*active, cfg);
    for (const auto& unknown : mapper.unknown_sources()) {
        LOGW("映射档里有无法识别的写法: %s", unknown.c_str());
    }

    dji::RcLink link;
    dji::RawState state;
    dji::PadOutput output;
    double last_connect_try = 0.0;
    double last_stats_log = 0.0;
    double last_profile_check = 0.0;
    std::string current_profile = active->name;
    int connect_failures = 0;
    const double started = dji::now_seconds();

    while (g_running) {
        const double now = dji::now_seconds();
        if (opt.seconds > 0 && now - started > opt.seconds) {
            LOGI("已达 --seconds 设定时长, 退出");
            break;
        }

        if (!link.connected() && now - last_connect_try > 1.0) {
            last_connect_try = now;
            if (link.connect(cfg.port, cfg.schedule, error)) {
                connect_failures = 0;
                LOGI("遥控器已连接 (端口 %s)", link.port().c_str());
            } else if (++connect_failures % 10 == 1) {
                LOGW("等待遥控器: %s", error.c_str());
            }
        }

        if (link.connected()) {
            if (!link.step(state)) {
                LOGW("链路异常, 重新连接");
                link.disconnect();
                state = dji::RawState{};
            }
        }

        if (cfg.auto_profile && now - last_profile_check > 0.5) {
            last_profile_check = now;
            std::string process, title;
            if (foreground_window(process, title)) {
                const std::string match = match_auto_profile(profile_names, cache,
                                                             to_lower(process), to_lower(title));
                if (!match.empty() && match != current_profile) {
                    const dji::Profile* swapped = resolve_profile(match, cache, error);
                    if (swapped != nullptr) {
                        LOGI("按前台窗口 %s 切换到映射档 %s", process.c_str(), match.c_str());
                        mapper.configure(*swapped, cfg);
                        current_profile = match;
                    }
                }
            }
        }

        if (state.stale(now, cfg.watchdog_ms / 1000.0)) {
            output.clear();
        } else {
            mapper.update(state, now, output);
        }
        if (!pad.submit(output)) {
            LOGE("虚拟手柄更新失败");
            return 3;
        }

        const double log_gap = link.connected() ? 2.0 : 10.0;
        if (now - last_stats_log > log_gap) {
            last_stats_log = now;
            const auto& stats = link.stats();
            std::printf("[%6.1fs] %s hz=%5.1f rtt_avg=%4.1fms timeouts=%llu bad=%llu batt=%3d%% "
                        "layer=%s -> %s\n",
                        now - started, link.connected() ? link.port().c_str() : "未连接",
                        stats.measured_hz, stats.avg_rtt_ms,
                        static_cast<unsigned long long>(stats.timeouts),
                        static_cast<unsigned long long>(stats.frames_bad), state.battery,
                        current_profile.c_str(), output.describe().c_str());
            std::fflush(stdout);
        }
        dji::sleep_ms(1);
    }

    LOGI("正在收尾: 发送 %llu 帧, 失败 %llu 次", static_cast<unsigned long long>(pad.updates()),
         static_cast<unsigned long long>(pad.failures()));
    link.disconnect();
    pad.stop();
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCtrlHandler(control_handler, TRUE);
    // Default timer resolution is ~15 ms, which would cap the poll loop near 60 Hz.
    timeBeginPeriod(1);

    Options opt;
    if (!parse_args(argc, argv, opt)) {
        print_usage();
        return 1;
    }
    prepare_paths(opt);

    std::string error;
    if (!ensure_profile_files(error)) {
        std::printf("初始化映射档失败: %s\n", error.c_str());
        return 1;
    }
    if (opt.write_defaults) {
        GlobalConfig cfg;
        if (!load_or_create_config(cfg, error)) {
            std::printf("初始化配置失败: %s\n", error.c_str());
            return 1;
        }
        std::printf("已准备: %s\n        %s\\default.ini  %s\\shooter.ini\n", g_config_path.c_str(),
                    g_profiles_dir.c_str(), g_profiles_dir.c_str());
        return 0;
    }

    GlobalConfig cfg;
    if (!load_or_create_config(cfg, error)) {
        std::printf("配置不可用且无法生成: %s\n", error.c_str());
        return 1;
    }
    dji::log_set_level(static_cast<dji::Level>(opt.mode == "watch" ? 3 : cfg.verbosity));
    if (!cfg.log_file.empty()) {
        const std::string path = g_config_path.substr(0, g_config_path.find_last_of('\\')) + "\\" +
                                 cfg.log_file;
        if (!dji::log_open_file(path.c_str())) {
            std::printf("日志文件打不开: %s\n", path.c_str());
        }
    }

    if (opt.mode == "scan") return mode_scan(opt);
    if (opt.mode == "watch") return mode_watch(opt, cfg);
    if (opt.mode == "calibrate") return mode_calibrate(opt, cfg);
    if (opt.mode == "probe") return mode_probe(opt, cfg);
    if (opt.mode == "raw") return mode_raw(opt);
    if (opt.mode == "legacy") return mode_legacy(opt);
    if (opt.mode == "bulk") return dji::probe_bulk(opt.seconds > 0 ? opt.seconds : 120);
    if (opt.mode == "sweep") return mode_sweep(opt);
    if (opt.mode == "baudsweep") return mode_baud_sweep(opt);
    if (opt.mode == "try") return mode_try(opt);
    if (opt.mode == "listen") return mode_listen(opt);
    if (opt.mode == "libusb")
        return dji::probe_libusb(opt.seconds > 0 ? opt.seconds : 120, 0x2CA3, 0x1020);
    return mode_run(opt, cfg);
}
