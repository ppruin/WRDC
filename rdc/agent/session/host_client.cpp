/**
 * @file host_client.cpp
 * @brief 实现 agent/session/host_client 相关的类型、函数与流程。
 */

#include "host_client.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#endif

#include "desktop_streamer.hpp"
#include "../encoder/bgra_to_nv12_converter.hpp"
#include "../platform/windows/d3d11_desktop_frame_reader.hpp"
#include "../platform/windows/dxgi_desktop_duplicator.hpp"
#include "../platform/windows/h264_video_encoder.hpp"
#include "../../protocol/common/buffer_utils.hpp"
#include "../../protocol/common/console_logger.hpp"

namespace rdc {

namespace {

/**
 * @brief 读取环境变量无符号整数。
 * @param name 名称字符串。
 * @param fallback 回退值。
 * @return 返回对应结果。
 */
template <typename Unsigned>
Unsigned ReadEnvUnsigned(const char* name, const Unsigned fallback)
{
    static_assert(std::is_unsigned_v<Unsigned>, "ReadEnvUnsigned 仅支持无符号整数类型");

    if (const char* value = std::getenv(name); value != nullptr)
    {
        try {
            return static_cast<Unsigned>(std::stoull(value));
        } 
    	catch (...) {
            return fallback;
        }
    }

    return fallback;
}

/**
 * @brief 读取环境变量字符串。
 * @param name 名称字符串。
 * @return 返回字符串结果；未设置时返回空字符串。
 */
std::string ReadEnvString(const char* name) {
    if (const char* value = std::getenv(name); value != nullptr) {
        return std::string(value);
    }

    return {};
}

/**
 * @brief 读取 H.264 编码后端环境变量。
 * @return 返回编码后端枚举值。
 */
agent::platform::windows::H264EncoderBackend ReadEnvH264EncoderBackend() {
    std::string value = ReadEnvString("RDC_H264_ENCODER");
    if (value.empty()) {
        value = ReadEnvString("RDC_ENCODER_BACKEND");
    }

    std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    if (value == "mf" || value == "mediafoundation" || value == "media_foundation") {
        return agent::platform::windows::H264EncoderBackend::MediaFoundation;
    }

    if (value == "x264") {
        return agent::platform::windows::H264EncoderBackend::X264;
    }

    return agent::platform::windows::H264EncoderBackend::Auto;
}

/**
 * @brief 判断采集SmokeEnabled是否满足条件。
 * @return 返回是否成功或条件是否满足。
 */
bool IsCaptureSmokeEnabled() 
{
    return std::getenv("RDC_CAPTURE_SMOKE") != nullptr || std::getenv("RDC_ENCODE_SMOKE") != nullptr;
}

/**
 * @brief 判断采集SmokeOnlyEnabled是否满足条件。
 * @return 返回是否成功或条件是否满足。
 */
bool IsCaptureSmokeOnlyEnabled() 
{
    return std::getenv("RDC_CAPTURE_SMOKE_ONLY") != nullptr || std::getenv("RDC_ENCODE_SMOKE_ONLY") != nullptr;
}

/**
 * @brief 判断编码SmokeEnabled是否满足条件。
 * @return 返回是否成功或条件是否满足。
 */
bool IsEncodeSmokeEnabled() 
{
    return std::getenv("RDC_ENCODE_SMOKE") != nullptr;
}

/**
 * @brief 判断是否为已知控制通道标签。
 * @param channel_label 数据通道标签。
 * @return 返回是否成功或条件是否满足。
 */
bool IsControlChannelLabel(const std::string_view channel_label) {
    return channel_label == "control" || channel_label == "control_rt";
}

<<<<<<< HEAD
/**
 * @brief 判断是否应记录高频控制输入追踪日志。
 * @param message_count 当前累计消息数。
 * @return 返回是否成功或条件是否满足。
 */
bool ShouldLogHighFrequencyControlInput(const std::uint64_t message_count) {
    return message_count <= 5 || message_count % 120 == 0;
}

/**
 * @brief 判断是否应记录输入队列积压告警。
 * @param pending_count 当前待处理队列长度。
 * @return 返回是否成功或条件是否满足。
 */
bool ShouldWarnInputQueueBacklog(const std::size_t pending_count) {
    return pending_count == 16 || pending_count == 32 || pending_count % 64 == 0;
}

=======
>>>>>>> ec6c746a58750b061c0e595b5410919ddc2500b1
#ifdef _WIN32
/**
 * @brief 发送单条 Windows 输入事件并在失败时尝试旧版 Win32 回退路径。
 * @tparam LegacyInvoker 旧版回退调用对象类型。
 * @param input 待注入的输入结构体。
 * @param input_name 输入事件名称。
 * @param legacy_invoker 旧版回退调用对象。
 * @return 返回是否成功或条件是否满足。
 */
template <typename LegacyInvoker>
bool SendSingleInput(const INPUT& input, const std::string_view input_name, LegacyInvoker&& legacy_invoker) 
{
    INPUT mutable_input = input;
    SetLastError(ERROR_SUCCESS);

    if (SendInput(1, &mutable_input, sizeof(INPUT)) == 1)
        return true;

    const DWORD error_code = GetLastError();

    if ((error_code == ERROR_ACCESS_DENIED || error_code == ERROR_SUCCESS) &&
        std::forward<LegacyInvoker>(legacy_invoker)()) 
    {
        return true;
    }

    protocol::common::WriteErrorLine("主机端注入 " + std::string(input_name) + " 失败，" + 
        (error_code == ERROR_SUCCESS
			? std::string("未返回系统错误，可能被 UIPI 或安全桌面阻止")
			: std::string("错误码=" + std::to_string(static_cast<unsigned long>(error_code)))
        )
    );
    return false;
}

/**
 * @brief 在当前线程作用域内附加输入桌面。
 */
class InputDesktopScope 
{

    HDESK desktop_ = nullptr;
    bool attached_ = false;
    DWORD last_error_code_ = ERROR_SUCCESS;

public:
    /**
     * @brief 构造 InputDesktopScope 对象并尝试附加输入桌面。
     */
    InputDesktopScope() 
	{
        desktop_ = OpenInputDesktop(0, FALSE, GENERIC_ALL);
        if (desktop_ == nullptr) 
        {
            last_error_code_ = GetLastError();
            return;
        }

        if (SetThreadDesktop(desktop_) != FALSE) 
        {
            attached_ = true;
            last_error_code_ = ERROR_SUCCESS;
            return;
        }

        last_error_code_ = GetLastError();
    }

    /**
     * @brief 析构 InputDesktopScope 对象并释放桌面句柄。
     */
    ~InputDesktopScope() 
	{
        if (desktop_ != nullptr)
            CloseDesktop(desktop_);
    }

    /**
     * @brief 禁止拷贝构造。
     */
    InputDesktopScope(const InputDesktopScope&) = delete;

    /**
     * @brief 禁止拷贝赋值。
     * @return 返回对象自身引用。
     */
    InputDesktopScope& operator=(const InputDesktopScope&) = delete;

    /**
     * @brief 判断当前线程是否已成功附加输入桌面。
     * @return 返回是否成功或条件是否满足。
     */
    bool IsAttached() const 
	{
        return attached_;
    }

    /**
     * @brief 获取最近一次附加输入桌面的错误码。
     * @return 返回对应结果。
     */
    DWORD GetLastErrorCode() const 
	{
        return last_error_code_;
    }
};

/**
 * @brief 解析捕获输出在虚拟桌面中的坐标范围。
 * @param output_index 显示输出索引。
 * @return 返回可用结果；失败时返回空值。
 */
std::optional<HostClient::DesktopOutputBounds> ResolveOutputBounds(const std::uint32_t output_index) 
{
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    const HRESULT factory_hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));

    if (FAILED(factory_hr)) 
        return std::nullopt;

    std::uint32_t attached_output_count = 0;
    for (UINT adapter_index = 0;; ++adapter_index) 
    {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        const HRESULT adapter_hr = factory->EnumAdapters1(adapter_index, &adapter);
        if (adapter_hr == DXGI_ERROR_NOT_FOUND)
            break;

        if (FAILED(adapter_hr))
            return std::nullopt;

        for (UINT local_output_index = 0;; ++local_output_index) 
        {
            Microsoft::WRL::ComPtr<IDXGIOutput> output;
            const HRESULT output_hr = adapter->EnumOutputs(local_output_index, &output);

            if (output_hr == DXGI_ERROR_NOT_FOUND)
                break;

            if (FAILED(output_hr))
                return std::nullopt;

            DXGI_OUTPUT_DESC output_desc{};
            const HRESULT desc_hr = output->GetDesc(&output_desc);

            if (FAILED(desc_hr) || !output_desc.AttachedToDesktop)
                continue;

            if (attached_output_count == output_index) 
            {
                const auto width = output_desc.DesktopCoordinates.right - output_desc.DesktopCoordinates.left;
                const auto height = output_desc.DesktopCoordinates.bottom - output_desc.DesktopCoordinates.top;
                if (width <= 0 || height <= 0)
                    return std::nullopt;

                return HostClient::DesktopOutputBounds{
                    .left = output_desc.DesktopCoordinates.left,
                    .top = output_desc.DesktopCoordinates.top,
                    .width = width,
                    .height = height,
                };
            }

            ++attached_output_count;
        }
    }

    return std::nullopt;
}

/**
 * @brief 将桌面像素坐标转换为 SendInput 绝对坐标。
 * @param value 输入坐标值。
 * @param origin 虚拟桌面原点。
 * @param span 虚拟桌面跨度。
 * @return 返回对应结果。
 */
LONG ToAbsoluteMouseCoordinate(const int value, const int origin, const int span) 
{
    if (span <= 1)
        return 0;

    const auto relative = static_cast<double>(value - origin);
    const auto normalized = std::clamp(relative / static_cast<double>(span - 1), 0.0, 1.0);
    return static_cast<LONG>(std::lround(normalized * 65535.0));
}

/**
 * @brief 解析浏览器键盘代码对应的 Windows 虚拟键值。
 * @param code 浏览器 KeyboardEvent.code。
 * @return 返回可用结果；失败时返回空值。
 */
std::optional<WORD> MapBrowserCodeToVirtualKey(const std::string_view code) {
    if (code.size() == 4 && code.rfind("Key", 0) == 0 &&
        std::isalpha(static_cast<unsigned char>(code[3])) != 0) 
    {
        return static_cast<WORD>(std::toupper(static_cast<unsigned char>(code[3])));
    }

    if (code.size() == 6 && code.rfind("Digit", 0) == 0 &&
        std::isdigit(static_cast<unsigned char>(code[5])) != 0) 
    {
        return static_cast<WORD>(code[5]);
    }

    if (code.size() == 7 && code.rfind("Numpad", 0) == 0 &&
        std::isdigit(static_cast<unsigned char>(code[6])) != 0) 
    {
        return static_cast<WORD>(VK_NUMPAD0 + (code[6] - '0'));
    }

    if (code.size() >= 2 && code[0] == 'F') 
    {
        try {
            const auto function_index = std::stoi(std::string(code.substr(1)));

            if (function_index >= 1 && function_index <= 24)
                return static_cast<WORD>(VK_F1 + function_index - 1);
        } 
    	catch (...) { }
    }

    const std::pair<std::string_view, WORD> mappings[] = {
        {"Enter", VK_RETURN},
        {"NumpadEnter", VK_RETURN},
        {"Tab", VK_TAB},
        {"Space", VK_SPACE},
        {"Backspace", VK_BACK},
        {"Escape", VK_ESCAPE},
        {"CapsLock", VK_CAPITAL},
        {"NumLock", VK_NUMLOCK},
        {"ScrollLock", VK_SCROLL},
        {"Pause", VK_PAUSE},
        {"Insert", VK_INSERT},
        {"Delete", VK_DELETE},
        {"Home", VK_HOME},
        {"End", VK_END},
        {"PageUp", VK_PRIOR},
        {"PageDown", VK_NEXT},
        {"ArrowUp", VK_UP},
        {"ArrowDown", VK_DOWN},
        {"ArrowLeft", VK_LEFT},
        {"ArrowRight", VK_RIGHT},
        {"ShiftLeft", VK_LSHIFT},
        {"ShiftRight", VK_RSHIFT},
        {"ControlLeft", VK_LCONTROL},
        {"ControlRight", VK_RCONTROL},
        {"AltLeft", VK_LMENU},
        {"AltRight", VK_RMENU},
        {"MetaLeft", VK_LWIN},
        {"MetaRight", VK_RWIN},
        {"ContextMenu", VK_APPS},
        {"PrintScreen", VK_SNAPSHOT},
        {"Semicolon", VK_OEM_1},
        {"Equal", VK_OEM_PLUS},
        {"Comma", VK_OEM_COMMA},
        {"Minus", VK_OEM_MINUS},
        {"Period", VK_OEM_PERIOD},
        {"Slash", VK_OEM_2},
        {"Backquote", VK_OEM_3},
        {"BracketLeft", VK_OEM_4},
        {"Backslash", VK_OEM_5},
        {"BracketRight", VK_OEM_6},
        {"Quote", VK_OEM_7},
        {"IntlBackslash", VK_OEM_102},
        {"NumpadMultiply", VK_MULTIPLY},
        {"NumpadAdd", VK_ADD},
        {"NumpadSubtract", VK_SUBTRACT},
        {"NumpadDecimal", VK_DECIMAL},
        {"NumpadDivide", VK_DIVIDE},
    };

    for (const auto& [candidate, vk] : mappings) 
    {
        if (candidate == code)
            return vk;
    }

    return std::nullopt;
}

/**
 * @brief 判断是否为扩展按键。
 * @param code 浏览器 KeyboardEvent.code。
 * @return 返回是否成功或条件是否满足。
 */
bool IsExtendedBrowserKeyCode(const std::string_view code) 
{
    return code == "NumpadEnter" ||
           code == "ControlRight" ||
           code == "AltRight" ||
           code == "Insert" ||
           code == "Delete" ||
           code == "Home" ||
           code == "End" ||
           code == "PageUp" ||
           code == "PageDown" ||
           code == "ArrowUp" ||
           code == "ArrowDown" ||
           code == "ArrowLeft" ||
           code == "ArrowRight" ||
           code == "MetaLeft" ||
           code == "MetaRight" ||
           code == "ContextMenu" ||
           code == "PrintScreen" ||
           code == "NumpadDivide";
}

/**
 * @brief 发送按键输入。
 * @param code 浏览器 KeyboardEvent.code。
 * @param pressed 是否按下。
 * @return 返回是否成功或条件是否满足。
 */
bool SendKeyboardInputEvent(const std::string_view code, const bool pressed) 
{
    const auto vk = MapBrowserCodeToVirtualKey(code);
<<<<<<< HEAD
    if (!vk.has_value()) {
        protocol::common::WriteErrorLine("主机端无法映射浏览器按键代码: code=" +
                                         std::string(code) +
                                         ", pressed=" + (pressed ? "true" : "false"));
        return false;
    }
=======
    if (!vk.has_value()) 
        return false;
>>>>>>> ec6c746a58750b061c0e595b5410919ddc2500b1

    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = *vk;
    input.ki.wScan = static_cast<WORD>(MapVirtualKeyW(*vk, MAPVK_VK_TO_VSC));
    input.ki.dwFlags = pressed ? 0 : KEYEVENTF_KEYUP;

    if (IsExtendedBrowserKeyCode(code))
        input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;

    return SendSingleInput(input, pressed ? "键盘按下" : "键盘抬起", [vk = *vk, scan = input.ki.wScan, pressed, code]() {
        BYTE flags = pressed ? 0 : KEYEVENTF_KEYUP;
        if (IsExtendedBrowserKeyCode(code))
            flags = static_cast<BYTE>(flags | KEYEVENTF_EXTENDEDKEY);

        keybd_event(static_cast<BYTE>(vk), static_cast<BYTE>(scan), flags, 0);
        return true;
    });
}

/**
 * @brief 发送鼠标按键输入。
 * @param button 浏览器鼠标按键编号。
 * @param pressed 是否按下。
 * @return 返回是否成功或条件是否满足。
 */
bool SendMouseButtonInputEvent(const int button, const bool pressed) 
{
    DWORD flags = 0;
    switch (button) 
    {
	    case 0:
	        flags = pressed ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
	        break;
	    case 1:
	        flags = pressed ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP;
	        break;
	    case 2:
	        flags = pressed ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
	        break;
	    default:
<<<<<<< HEAD
            protocol::common::WriteErrorLine("主机端收到不支持的鼠标按键编号: button=" +
                                             std::to_string(button) +
                                             ", pressed=" + (pressed ? "true" : "false"));
=======
>>>>>>> ec6c746a58750b061c0e595b5410919ddc2500b1
	        return false;
    }

    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = flags;
    return SendSingleInput(input, pressed ? "鼠标按下" : "鼠标抬起", [flags]() {
        mouse_event(flags, 0, 0, 0, 0);
        return true;
    });
}

/**
 * @brief 发送滚轮输入。
 * @param delta 滚轮增量。
 * @param horizontal 是否水平滚轮。
 * @return 返回是否成功或条件是否满足。
 */
bool SendMouseWheelInputEvent(const int delta, const bool horizontal) 
{
    if (delta == 0)
        return true;

    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = horizontal ? MOUSEEVENTF_HWHEEL : MOUSEEVENTF_WHEEL;
    input.mi.mouseData = static_cast<DWORD>(delta);
    return SendSingleInput(input, horizontal ? "水平滚轮" : "垂直滚轮", [horizontal, delta]() {
        mouse_event(horizontal ? MOUSEEVENTF_HWHEEL : MOUSEEVENTF_WHEEL,
                    0,
                    0,
                    static_cast<DWORD>(delta),
                    0);
        return true;
    });
}
#endif

/**
 * @brief 执行 Host客户端 相关处理。
 * @param signal_url 信令服务地址。
 */
}  // namespace

HostClient::HostClient(std::string signal_url, std::string device_id)
    : signal_url_(std::move(signal_url)),
      device_id_(std::move(device_id)) {
}

/**
 * @brief 运行相关流程。
 * @return 返回状态码或退出码。
 */
int HostClient::Run() 
{
    if (RunDesktopCaptureSmokeIfEnabled()) 
        return 0;

    stop_requested_.store(false);
    {
        std::scoped_lock lock(input_control_mutex_);
        pending_input_controls_.clear();
    }

#ifdef _WIN32
    desktop_output_index_ = ReadEnvUnsigned<std::uint32_t>("RDC_CAPTURE_OUTPUT", 0);
    if (const auto bounds = ResolveOutputBounds(desktop_output_index_); bounds.has_value() && bounds->IsValid()) 
    {
        desktop_output_bounds_ = *bounds;
        desktop_output_bounds_ready_ = true;
        protocol::common::WriteInfoLine(
            "主机端鼠标同步已绑定捕获输出: index=" + std::to_string(desktop_output_index_) +
            ", rect=(" + std::to_string(desktop_output_bounds_.left) + "," +
            std::to_string(desktop_output_bounds_.top) + "," +
            std::to_string(desktop_output_bounds_.width) + "x" +
            std::to_string(desktop_output_bounds_.height) + ")");
    } 
	else
	{
        protocol::common::WriteErrorLine(
            "主机端无法解析捕获输出坐标范围，浏览器鼠标同步将不可用: output_index=" +
            std::to_string(desktop_output_index_));
    }
#endif

    StartInputDispatchers();

    desktop_streamer_ = std::make_unique<agent::session::DesktopStreamer>(
        [this]() {
            return CollectActiveVideoSessions();
        },
        agent::session::DesktopStreamerConfig{
            .output_index = desktop_output_index_,
            .encoder_backend = ReadEnvH264EncoderBackend(),
        });
    desktop_streamer_->Start();

    ConfigureSocket();

    protocol::common::WriteInfoLine("主机端正在连接 " + signal_url_ + "，设备 ID: " + device_id_);
    signal_client_.Connect(signal_url_);

    SendJson(Json{
        {"type", protocol::kRegisterDevice},
        {"deviceId", device_id_},
        {"capabilities", Json{
                             {"controlReliableChannel", true},
                             {"controlRealtimeChannel", true}
                         }}
    });

    std::unique_lock lock(mutex_);
    cv_.wait(lock, [this] {
        return stop_requested_.load();
    });
    lock.unlock();

    if (desktop_streamer_ != nullptr) {
        desktop_streamer_->Stop();
        desktop_streamer_.reset();
    }

    protocol::common::ForEachValue(sessions_, [](auto& entry) {
        auto& runtime = entry.second;
        if (runtime.peer_session != nullptr) {
            runtime.peer_session->Close();
        }
    });
    sessions_.clear();

    signal_client_.Close();
    StopInputDispatchers();

    return 0;
}

/**
 * @brief 配置套接字。
 */
void HostClient::ConfigureSocket() 
{
    signal_client_.SetClosedHandler([this]() {
        protocol::common::WriteInfoLine("主机端信令连接已关闭");
        Stop();
    });

    signal_client_.SetErrorHandler([](const std::string& error) {
        protocol::common::WriteErrorLine("主机端信令错误: " + error);
    });

    signal_client_.SetMessageHandler([this](const Json& message) {
        HandleMessage(message);
    });
}

/**
 * @brief 运行桌面采集SmokeIfEnabled。
 * @return 返回是否成功或条件是否满足。
 */
bool HostClient::RunDesktopCaptureSmokeIfEnabled() const 
{
    if (!IsCaptureSmokeEnabled())
        return false;

    const auto output_index = ReadEnvUnsigned<std::uint32_t>("RDC_CAPTURE_OUTPUT", 0);
    const auto frame_limit = ReadEnvUnsigned<std::uint32_t>("RDC_CAPTURE_SMOKE_FRAMES", 1);
    const auto timeout_ms = ReadEnvUnsigned<std::uint32_t>("RDC_CAPTURE_TIMEOUT_MS", 1000);
    const std::uint32_t configured_attempts =
        ReadEnvUnsigned<std::uint32_t>("RDC_CAPTURE_SMOKE_ATTEMPTS", frame_limit * 5);
    const std::uint32_t max_attempts = configured_attempts < frame_limit ? frame_limit : configured_attempts;
    const auto encoder_backend = ReadEnvH264EncoderBackend();

    protocol::common::WriteInfoLine("主机端采集冒烟测试开始: 输出=" + std::to_string(output_index) +
                                    ", 帧数=" + std::to_string(frame_limit) +
                                    ", 超时毫秒=" + std::to_string(timeout_ms) +
                                    ", 最大尝试次数=" + std::to_string(max_attempts) +
                                    ", 编码后端=" + std::string(agent::platform::windows::ToString(encoder_backend)));

    agent::platform::windows::DxgiDesktopDuplicator capturer(output_index);
    agent::platform::windows::D3D11DesktopFrameReader frame_reader;
    agent::encoder::BgraToNv12Converter nv12_converter;
    std::unique_ptr<agent::platform::windows::H264VideoEncoder> encoder;

    std::uint32_t captured_frames = 0;
    std::uint32_t attempts = 0;
    while (captured_frames < frame_limit && attempts < max_attempts) 
    {
        ++attempts;
        auto frame = capturer.CaptureNextFrame(std::chrono::milliseconds(timeout_ms));

        if (!frame.has_value()) 
        {
            protocol::common::WriteInfoLine("主机端采集冒烟测试第 " + std::to_string(attempts) + " 次尝试超时");
            continue;
        }

        auto raw_frame = frame_reader.Read(*frame);
        std::size_t encoded_frame_count = 0;
        std::size_t total_encoded_bytes = 0;
        std::size_t key_frame_count = 0;

        if (IsEncodeSmokeEnabled())
        {
            auto nv12_frame = nv12_converter.Convert(raw_frame);
            if (encoder == nullptr)
                encoder = std::make_unique<agent::platform::windows::H264VideoEncoder>(
                    agent::platform::windows::H264EncoderConfig{
                        .width = nv12_frame.width,
                        .height = nv12_frame.height,
                        .fps_num = 30,
                        .fps_den = 1,
                        .bitrate = 4'000'000,
                        .gop_size = 60,
                        .backend = encoder_backend,
                    });

            auto collect_stats = [&](const auto& encoded_frame) {
                ++encoded_frame_count;
                total_encoded_bytes += encoded_frame.bytes.size();
                if (encoded_frame.is_key_frame) {
                    ++key_frame_count;
                }
            };

            encoder->EncodeEach(nv12_frame, collect_stats);
            if (encoded_frame_count == 0)
                encoder->DrainEach(collect_stats);
        }

        ++captured_frames;
        std::string smoke_line = "主机端采集冒烟测试帧 " + std::to_string(captured_frames) + "/" +
                                 std::to_string(frame_limit) +
                                 ": 分辨率=" + std::to_string(frame->width) + "x" + std::to_string(frame->height) +
                                 ", 格式=" + std::string(agent::capture::ToString(frame->pixel_format)) +
                                 ", 回读格式=" + std::string(agent::encoder::ToString(raw_frame.pixel_format)) +
                                 ", 步长=" + std::to_string(raw_frame.stride_bytes) +
                                 ", 字节数=" + std::to_string(raw_frame.bytes.size()) +
                                 ", 脏矩形=" + std::to_string(frame->dirty_rects.size()) +
                                 ", 位移矩形=" + std::to_string(frame->move_rects.size()) +
                                 ", 累积帧数=" + std::to_string(frame->accumulated_frames) +
                                 ", 显示时间戳=" + std::to_string(frame->present_qpc_ticks);

        if (IsEncodeSmokeEnabled())
            smoke_line += ", 编码帧数=" + std::to_string(encoded_frame_count) +
                          ", 编码字节数=" + std::to_string(total_encoded_bytes) +
                          ", 关键帧数=" + std::to_string(key_frame_count) +
                          ", 实际编码后端=" +
                              std::string(encoder != nullptr ? encoder->ActiveBackendName()
                                                             : agent::platform::windows::ToString(encoder_backend));

        protocol::common::WriteInfoLine(smoke_line);
    }

    if (captured_frames == 0)
        throw std::runtime_error("桌面采集冒烟测试未产生任何帧");

    if (IsCaptureSmokeOnlyEnabled()) 
    {
        protocol::common::WriteInfoLine("主机端采集冒烟测试已完成，仅执行采集验证");
        return true;
    }

    return false;
}

/**
 * @brief 收集Active视频Sessions。
 * @return 返回结果集合。
 */
std::vector<std::shared_ptr<PeerSession>> HostClient::CollectActiveVideoSessions() {
    std::vector<std::shared_ptr<PeerSession>> active_sessions;

    std::scoped_lock lock(mutex_);
    active_sessions.reserve(sessions_.size());
    for (const auto& [session_id, runtime] : sessions_) {
        (void)session_id;
        if (runtime.peer_session != nullptr) {
            active_sessions.push_back(runtime.peer_session);
        }
    }

    return active_sessions;
}

/**
 * @brief 启动输入派发线程组。
 */
void HostClient::StartInputDispatchers() {
    if (!input_thread_.joinable()) {
        input_thread_ = std::thread([this]() {
            RunInputLoop();
        });
    }
}

/**
 * @brief 停止输入派发线程组。
 */
void HostClient::StopInputDispatchers() {
    input_control_cv_.notify_all();

    if (input_thread_.joinable()) {
        input_thread_.join();
    }
}

/**
 * @brief 将输入控制消息加入派发队列。
 * @param payload 协议负载数据。
 */
void HostClient::QueueInputControl(const Json& payload) {
    const std::string type = payload.value("type", "");
    if (type == "key_down" || type == "key_up") {
        QueueKeyboardInputControl(payload);
        return;
    }

    QueueMouseInputControl(payload);
}

/**
 * @brief 将键盘控制消息加入派发队列。
 * @param payload 协议负载数据。
 */
void HostClient::QueueKeyboardInputControl(const Json& payload) {
<<<<<<< HEAD
    std::size_t pending_count = 0;

    {
        std::scoped_lock lock(input_control_mutex_);
        pending_input_controls_.push_back(payload);
        pending_count = pending_input_controls_.size();
    }

    if (pending_count >= 16 && ShouldWarnInputQueueBacklog(pending_count)) {
        protocol::common::WriteLogLine(
            protocol::common::LogSeverity::Warning,
            "主机端输入队列积压: pending=" + std::to_string(pending_count) +
                ", latestType=" + payload.value("type", ""));
=======
    {
        std::scoped_lock lock(input_control_mutex_);
        pending_input_controls_.push_back(payload);
>>>>>>> ec6c746a58750b061c0e595b5410919ddc2500b1
    }

    input_control_cv_.notify_one();
}

/**
 * @brief 将鼠标控制消息加入派发队列。
 * @param payload 协议负载数据。
 */
void HostClient::QueueMouseInputControl(const Json& payload) {
    bool queued = false;
<<<<<<< HEAD
    std::size_t pending_count = 0;
=======
>>>>>>> ec6c746a58750b061c0e595b5410919ddc2500b1
    {
        std::scoped_lock lock(input_control_mutex_);
        const std::string type = payload.value("type", "");

        if (type == "mouse_move") {
            if (!pending_input_controls_.empty() &&
                pending_input_controls_.back().value("type", "") == "mouse_move") {
                pending_input_controls_.back() = payload;
            } else {
                pending_input_controls_.push_back(payload);
            }
            queued = true;
        } else if (type == "mouse_button") {
            pending_input_controls_.push_back(payload);
            queued = true;
        } else if (type == "mouse_wheel") {
            if (!pending_input_controls_.empty() &&
                pending_input_controls_.back().value("type", "") == "mouse_wheel") {
                auto& merged_payload = pending_input_controls_.back();
                merged_payload["deltaX"] =
                    merged_payload.value("deltaX", 0) + payload.value("deltaX", 0);
                merged_payload["deltaY"] =
                    merged_payload.value("deltaY", 0) + payload.value("deltaY", 0);
                if (payload.contains("normalizedX")) {
                    merged_payload["normalizedX"] = payload["normalizedX"];
                }
                if (payload.contains("normalizedY")) {
                    merged_payload["normalizedY"] = payload["normalizedY"];
                }
                if (payload.contains("ts")) {
                    merged_payload["ts"] = payload["ts"];
                }
            } else {
                pending_input_controls_.push_back(payload);
            }
            queued = true;
        }
<<<<<<< HEAD

        pending_count = pending_input_controls_.size();
    }

    if (queued) {
        if (pending_count >= 16 && ShouldWarnInputQueueBacklog(pending_count)) {
            protocol::common::WriteLogLine(
                protocol::common::LogSeverity::Warning,
                "主机端输入队列积压: pending=" + std::to_string(pending_count) +
                    ", latestType=" + payload.value("type", ""));
        }
=======
    }

    if (queued) {
>>>>>>> ec6c746a58750b061c0e595b5410919ddc2500b1
        input_control_cv_.notify_one();
    }
}

/**
 * @brief 运行输入派发循环。
 */
void HostClient::RunInputLoop() {
#ifdef _WIN32
    InputDesktopScope input_desktop_scope;
    if (!input_desktop_scope.IsAttached()) {
        protocol::common::WriteErrorLine(
            "主机端输入线程附加输入桌面失败，远端键鼠同步可能不可用 (错误码=" +
            std::to_string(input_desktop_scope.GetLastErrorCode()) + ")");
    }
#endif

    while (true) {
        Json payload;

        {
            std::unique_lock lock(input_control_mutex_);
            input_control_cv_.wait(lock, [this] {
                return stop_requested_.load() || !pending_input_controls_.empty();
            });

            if (pending_input_controls_.empty()) {
                if (stop_requested_.load()) {
                    break;
                }

                continue;
            }

            payload = std::move(pending_input_controls_.front());
            pending_input_controls_.pop_front();
        }

        const std::string type = payload.value("type", "");
        if (type == "key_down" || type == "key_up") {
            ApplyQueuedKeyboardInput(payload);
        } else if (type == "mouse_move") {
            ApplyQueuedMouseMoveInput(payload);
        } else if (type == "mouse_button") {
            ApplyQueuedMouseButtonInput(payload);
        } else if (type == "mouse_wheel") {
            ApplyQueuedMouseWheelInput(payload);
        }
    }
}

/**
 * @brief 在键盘输入线程中应用排队的远端输入事件。
 * @param payload 协议负载数据。
 */
void HostClient::ApplyQueuedKeyboardInput(const Json& payload) {
#ifdef _WIN32
    const std::string type = payload.value("type", "");
    if (type == "key_down" || type == "key_up") {
        const auto code = payload.value("code", "");
        if (!code.empty()) {
            static_cast<void>(SendKeyboardInputEvent(code, type == "key_down"));
<<<<<<< HEAD
        } else {
            protocol::common::WriteErrorLine("主机端收到缺少 code 的键盘控制负载: " + payload.dump());
=======
>>>>>>> ec6c746a58750b061c0e595b5410919ddc2500b1
        }
    }
#else
    (void)payload;
#endif
}

/**
 * @brief 在鼠标输入线程中应用排队的鼠标坐标同步事件。
 * @param payload 协议负载数据。
 */
void HostClient::ApplyQueuedMouseMoveInput(const Json& payload) {
    const double normalized_x = payload.value("normalizedX", -1.0);
    const double normalized_y = payload.value("normalizedY", -1.0);
    if (normalized_x >= 0.0 && normalized_y >= 0.0) {
        static_cast<void>(SyncRemoteMousePosition(normalized_x, normalized_y));
<<<<<<< HEAD
    } else {
        protocol::common::WriteErrorLine("主机端收到缺少归一化坐标的鼠标移动负载: " + payload.dump());
=======
>>>>>>> ec6c746a58750b061c0e595b5410919ddc2500b1
    }
}

/**
 * @brief 在鼠标输入线程中应用排队的鼠标按键事件。
 * @param payload 协议负载数据。
 */
void HostClient::ApplyQueuedMouseButtonInput(const Json& payload) {
    const double normalized_x = payload.value("normalizedX", -1.0);
    const double normalized_y = payload.value("normalizedY", -1.0);
    if (normalized_x >= 0.0 && normalized_y >= 0.0) {
        static_cast<void>(SyncRemoteMousePosition(normalized_x, normalized_y));
    }

#ifdef _WIN32
    static_cast<void>(SendMouseButtonInputEvent(payload.value("button", -1), payload.value("pressed", false)));
#else
    (void)payload;
#endif
}

/**
 * @brief 在鼠标输入线程中应用排队的鼠标滚轮事件。
 * @param payload 协议负载数据。
 */
void HostClient::ApplyQueuedMouseWheelInput(const Json& payload) {
    const double normalized_x = payload.value("normalizedX", -1.0);
    const double normalized_y = payload.value("normalizedY", -1.0);
    if (normalized_x >= 0.0 && normalized_y >= 0.0) {
        static_cast<void>(SyncRemoteMousePosition(normalized_x, normalized_y));
    }

#ifdef _WIN32
    static_cast<void>(SendMouseWheelInputEvent(payload.value("deltaY", 0), false));
    static_cast<void>(SendMouseWheelInputEvent(payload.value("deltaX", 0), true));
#else
    (void)payload;
#endif
}

/**
 * @brief 处理控制通道消息。
 * @param session_id 会话标识。
 * @param channel_label 数据通道标签。
 * @param payload 协议负载数据。
 */
void HostClient::HandleControlMessage(const std::string& session_id,
                                      const std::string_view channel_label,
                                      const Json& payload) {
    const std::string type = payload.value("type", "");
    const bool is_control_channel = IsControlChannelLabel(channel_label);

    const bool is_high_frequency_input =
        is_control_channel &&
        (type == "mouse_move" || type == "mouse_button" || type == "mouse_wheel" || type == "key_down" || type == "key_up");

<<<<<<< HEAD
    std::uint64_t high_frequency_input_count = 0;
    if (is_high_frequency_input) {
        std::scoped_lock lock(mutex_);
        if (const auto it = sessions_.find(session_id); it != sessions_.end()) {
            high_frequency_input_count = ++it->second.received_control_input_count;
        }
    }

    const bool should_log =
        !is_high_frequency_input || ShouldLogHighFrequencyControlInput(high_frequency_input_count);

    if (should_log) {
        if (is_high_frequency_input) {
            protocol::common::WriteInfoLine(
                "主机端控制输入 <- sessionId=" + session_id +
                ", channel=" + std::string(channel_label) +
                ", type=" + type +
                ", count=" + std::to_string(high_frequency_input_count));
        } else {
            protocol::common::WriteInfoLine("主机端数据 <- [" + std::string(channel_label) + "] " + payload.dump());
        }
    }
=======
    const bool should_log = !is_high_frequency_input;

    if (should_log) 
        protocol::common::WriteInfoLine("主机端数据 <- [" + std::string(channel_label) + "] " + payload.dump());
>>>>>>> ec6c746a58750b061c0e595b5410919ddc2500b1

    if (!is_control_channel) 
        return;

    if (type == "ping") 
    {
        std::shared_ptr<PeerSession> current_session;

        {
            std::scoped_lock lock(mutex_);
            if (const auto it = sessions_.find(session_id); it != sessions_.end())
                current_session = it->second.peer_session;
        }

        if (current_session != nullptr) 
        {
            current_session->SendControl(Json{
                {"type", "pong"},
                {"echoSeq", payload.value("seq", 0)}
            });
        }

        return;
    }

    if (type == "mouse_move") 
    {
        QueueInputControl(payload);
        return;
    }

    if (type == "mouse_button") 
    {
        QueueInputControl(payload);
        return;
    }

    if (type == "mouse_wheel") 
    {
        QueueInputControl(payload);
        return;
    }

    if (type == "key_down" || type == "key_up") 
    {
        QueueInputControl(payload);
        return;
    }

    protocol::common::WriteInfoLine("主机端暂未处理控制消息类型: " + type);
}

/**
 * @brief 处理经信令通道转发的控制消息。
 * @param session_id 会话标识。
 * @param signal_payload 信令负载对象。
 * @return 返回是否已识别并处理控制消息。
 */
bool HostClient::HandleSignaledControlMessage(const std::string& session_id, const Json& signal_payload) 
{
    if (!signal_payload.is_object() || signal_payload.value("kind", "") != "control")
        return false;

    const Json control_payload = signal_payload.value("data", Json::object());
    if (!control_payload.is_object()) 
    {
        protocol::common::WriteErrorLine("主机端收到格式错误的信令控制负载: sessionId=" + session_id + ", payload=" + signal_payload.dump());
        return true;
    }

    const std::string channel = signal_payload.value("channel", "control");
    HandleControlMessage(session_id, channel.empty() ? std::string_view("control") : std::string_view(channel), control_payload);
    return true;
}

/**
 * @brief 同步远端鼠标位置。
 * @param normalized_x 归一化横坐标。
 * @param normalized_y 归一化纵坐标。
 * @return 返回是否成功或条件是否满足。
 */
bool HostClient::SyncRemoteMousePosition(const double normalized_x, const double normalized_y) const 
{
#ifdef _WIN32
<<<<<<< HEAD
    if (!desktop_output_bounds_ready_ || !desktop_output_bounds_.IsValid()) {
        static std::atomic_uint64_t missing_bounds_failures{0};
        const auto failure_count = ++missing_bounds_failures;
        if (ShouldLogHighFrequencyControlInput(failure_count)) {
            protocol::common::WriteErrorLine(
                "主机端鼠标同步失败: 捕获输出边界未就绪, output_index=" +
                std::to_string(desktop_output_index_) +
                ", count=" + std::to_string(failure_count));
        }
        return false;
    }

    if (!std::isfinite(normalized_x) || !std::isfinite(normalized_y)) {
        protocol::common::WriteErrorLine(
            "主机端鼠标同步失败: 归一化坐标非法, x=" + std::to_string(normalized_x) +
            ", y=" + std::to_string(normalized_y));
        return false;
    }
=======
    if (!desktop_output_bounds_ready_ || !desktop_output_bounds_.IsValid())
        return false;

    if (!std::isfinite(normalized_x) || !std::isfinite(normalized_y))
        return false;
>>>>>>> ec6c746a58750b061c0e595b5410919ddc2500b1

    const auto clamped_x = std::clamp(normalized_x, 0.0, 1.0);
    const auto clamped_y = std::clamp(normalized_y, 0.0, 1.0);
    const auto max_x = desktop_output_bounds_.width > 0 ? desktop_output_bounds_.width - 1 : 0;
    const auto max_y = desktop_output_bounds_.height > 0 ? desktop_output_bounds_.height - 1 : 0;
    const auto target_x = desktop_output_bounds_.left + static_cast<int>(std::lround(clamped_x * max_x));
    const auto target_y = desktop_output_bounds_.top + static_cast<int>(std::lround(clamped_y * max_y));

    const int virtual_left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int virtual_top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int virtual_width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int virtual_height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
<<<<<<< HEAD
    if (virtual_width <= 0 || virtual_height <= 0) {
        protocol::common::WriteErrorLine(
            "主机端鼠标同步失败: 虚拟桌面尺寸非法, width=" + std::to_string(virtual_width) +
            ", height=" + std::to_string(virtual_height));
        return false;
    }
=======
    if (virtual_width <= 0 || virtual_height <= 0)
        return false;
>>>>>>> ec6c746a58750b061c0e595b5410919ddc2500b1

    if (SetCursorPos(target_x, target_y) != FALSE)
        return true;

    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dx = ToAbsoluteMouseCoordinate(target_x, virtual_left, virtual_width);
    input.mi.dy = ToAbsoluteMouseCoordinate(target_y, virtual_top, virtual_height);
    input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    return SendSingleInput(input, "鼠标移动", [target_x, target_y]() {
        return SetCursorPos(target_x, target_y) != FALSE;
    });
#else
    (void)normalized_x;
    (void)normalized_y;
    return false;
#endif
}

/**
 * @brief 处理消息。
 * @param message 待处理的消息对象。
 */
void HostClient::HandleMessage(const Json& message) 
{
    const std::string type = message.value("type", "");

    if (type == "hello" || type == "registered" || type == "heartbeat_ack") 
    {
        protocol::common::WriteInfoLine("主机端收到信令 <- " + message.dump());
        return;
    }

    if (type == "session_request") 
    {
        const std::string session_id = message.value("sessionId", "");
        protocol::common::WriteInfoLine("主机端接受会话 " + session_id);

        auto session = std::make_shared<PeerSession>(
            PeerRole::Host,
            session_id,
            [this, session_id](const Json& payload) {
                SendJson(Json{
                    {"type", protocol::kSignal},
                    {"sessionId", session_id},
                    {"payload", payload}
                });
            },
            [this, session_id](std::string_view channel_label, const Json& payload) {
                HandleControlMessage(session_id, channel_label, payload);
            });

        {
            std::scoped_lock lock(mutex_);
            sessions_[session_id] = SessionRuntime{
                .peer_session = session,
            };
        }

        session->Start();
        SendJson(Json{
            {"type", protocol::kAcceptSession},
            {"sessionId", session_id}
        });
        return;
    }

    if (type == protocol::kSignal) {
        const std::string session_id = message.value("sessionId", "");
        const Json signal_payload = message.value("payload", Json::object());
        std::shared_ptr<PeerSession> session;

        {
            std::scoped_lock lock(mutex_);
            if (const auto it = sessions_.find(session_id); it != sessions_.end())
                session = it->second.peer_session;
        }

        if (session != nullptr)
            if (!HandleSignaledControlMessage(session_id, signal_payload))
                session->HandleSignal(signal_payload);

        return;
    }

    if (type == "session_closed" || type == "session_failed" || type == "session_rejected") {
        const std::string session_id = message.value("sessionId", "");
        SessionRuntime runtime;

        {
            std::scoped_lock lock(mutex_);
            if (const auto it = sessions_.find(session_id); it != sessions_.end()) {
                runtime.peer_session = it->second.peer_session;
                sessions_.erase(it);
            }
        }

        if (runtime.peer_session != nullptr) {
            runtime.peer_session->Close();
        }

        protocol::common::WriteInfoLine("主机端会话结束 <- " + message.dump());
        return;
    }

    if (type == "error") {
        protocol::common::WriteErrorLine("主机端收到信令错误负载 <- " + message.dump());
        return;
    }

    protocol::common::WriteInfoLine("主机端忽略信令负载 <- " + message.dump());
}

/**
 * @brief 发送 JSON 消息。
 * @param message 待处理的消息对象。
 */
void HostClient::SendJson(const Json& message) {
    signal_client_.SendJson(message);
}

/**
 * @brief 停止相关流程。
 */
void HostClient::Stop() {
    {
        std::scoped_lock lock(mutex_);
        if (stop_requested_.load()) {
            return;
        }

        stop_requested_.store(true);
    }

    cv_.notify_all();
    input_control_cv_.notify_all();
}

}  // namespace rdc
