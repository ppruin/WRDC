/**
 * @file console_logger.cpp
 * @brief 实现 protocol/common/console_logger 相关的类型、函数与流程。
 */

#include "console_logger.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>

#include <rtc/global.hpp>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace rdc::protocol::common {

namespace {

/**
 * @brief 获取控制台Mutex。
 * @return 返回对应结果。
 */
std::mutex& GetConsoleMutex() {
    static std::mutex mutex;
    return mutex;
}

/**
 * @brief 获取 Release 错误日志输出状态。
 * @return 返回对应结果。
 */
struct ReleaseErrorLogState {
    std::mutex mutex;
    std::ofstream stream;
    std::filesystem::path path;
};

ReleaseErrorLogState& GetReleaseErrorLogState() {
    static ReleaseErrorLogState state;
    return state;
}

/**
 * @brief 判断是否应当执行SuppressRtcWarning。
 * @param level 日志级别。
 * @param message 待处理的消息对象。
 * @return 返回是否成功或条件是否满足。
 */
bool ShouldSuppressRtcWarning(const rtc::LogLevel level, const std::string_view message) {
    if (level != rtc::LogLevel::Warning) {
        return false;
    }

    return message.find("Unexpected local description in signaling state have-local-offer, ignoring") != std::string_view::npos ||
           message.find("AES-GCM for SRTP is not supported, falling back to default profile") != std::string_view::npos;
}

/**
 * @brief 写入行。
 * @param stream 目标输出流。
 * @param line 待输出的文本内容。
 */
void WriteLine(std::ostream& stream, const std::string_view line) {
    std::scoped_lock lock(GetConsoleMutex());
    stream << line << '\n';
}

/**
 * @brief 构建当前时间戳字符串。
 * @return 返回生成的字符串结果。
 */
std::string BuildTimestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto current_time = std::chrono::system_clock::to_time_t(now);

    std::tm local_time{};
#ifdef _WIN32
    localtime_s(&local_time, &current_time);
#else
    localtime_r(&current_time, &local_time);
#endif

    std::ostringstream oss;
    oss << std::put_time(&local_time, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

/**
 * @brief 构建用于文件名的时间戳字符串。
 * @return 返回生成的字符串结果。
 */
std::string BuildFileTimestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto current_time = std::chrono::system_clock::to_time_t(now);

    std::tm local_time{};
#ifdef _WIN32
    localtime_s(&local_time, &current_time);
#else
    localtime_r(&current_time, &local_time);
#endif

    std::ostringstream oss;
    oss << std::put_time(&local_time, "%Y%m%d-%H%M%S");
    return oss.str();
}

/**
 * @brief 将日志级别转换为中文表示。
 * @param severity 日志严重级别。
 * @return 返回对应结果。
 */
const char* ToChineseLevel(const LogSeverity severity) {
    switch (severity) {
    case LogSeverity::Info:
        return "信息";
    case LogSeverity::Warning:
        return "警告";
    case LogSeverity::Error:
        return "错误";
    case LogSeverity::Debug:
        return "详细";
    }

    return "日志";
}

/**
 * @brief 构建 release 错误日志行文本。
 * @param severity 日志严重级别。
 * @param line 原始日志内容。
 * @return 返回生成的字符串结果。
 */
std::string FormatReleaseLogLine(const LogSeverity severity, const std::string_view line) {
    return "[" + BuildTimestamp() + "][" + ToChineseLevel(severity) + "] " + std::string(line);
}

/**
 * @brief 解析当前可执行文件所在目录。
 * @return 返回对应结果。
 */
std::filesystem::path ResolveExecutableDirectory() {
#ifdef _WIN32
    std::wstring buffer(MAX_PATH, L'\0');
    DWORD length = 0;
    while (true) {
        length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            break;
        }

        if (length < buffer.size()) {
            buffer.resize(length);
            std::filesystem::path exe_path(buffer);
            if (exe_path.has_parent_path()) {
                return exe_path.parent_path();
            }
            return std::filesystem::current_path();
        }

        buffer.resize(buffer.size() * 2);
    }
#endif

    return std::filesystem::current_path();
}

/**
 * @brief 确保 release 错误日志文件已打开。
 * @return 返回结果流指针；失败时返回空指针。
 */
std::ofstream* EnsureReleaseErrorLogStream() {
    auto& state = GetReleaseErrorLogState();
    if (state.stream.is_open()) {
        return &state.stream;
    }

    const std::filesystem::path log_directory = ResolveExecutableDirectory() / "log";
    state.path = log_directory / (BuildFileTimestamp() + "_rdc.log");

    std::error_code ec;
    std::filesystem::create_directories(log_directory, ec);
    if (ec) {
        return nullptr;
    }

    const bool should_write_utf8_bom = !std::filesystem::exists(state.path, ec) ||
                                       (ec ? true : std::filesystem::file_size(state.path, ec) == 0);
    ec.clear();

    state.stream.open(state.path, std::ios::out | std::ios::app | std::ios::binary);
    if (!state.stream.is_open()) {
        return nullptr;
    }

    if (should_write_utf8_bom) {
        static constexpr unsigned char kUtf8Bom[] = {0xEF, 0xBB, 0xBF};
        state.stream.write(reinterpret_cast<const char*>(kUtf8Bom), sizeof(kUtf8Bom));
    }

    return &state.stream;
}

/**
 * @brief 向 release 错误日志文件追加一行。
 * @param severity 日志严重级别。
 * @param line 原始日志内容。
 */
void AppendReleaseErrorLine(const LogSeverity severity, const std::string_view line) {
    auto& state = GetReleaseErrorLogState();
    std::scoped_lock lock(state.mutex);

    std::ofstream* stream = EnsureReleaseErrorLogStream();
    if (stream == nullptr) {
        return;
    }

    *stream << FormatReleaseLogLine(severity, line) << '\n';
    stream->flush();
}

/**
 * @brief 初始化控制台日志器。
 */
}  // namespace

void InitializeConsoleLogger() {
#ifdef _DEBUG
    rtc::InitLogger(rtc::LogLevel::Warning, [](const rtc::LogLevel level, const rtc::string& message) {
        if (ShouldSuppressRtcWarning(level, message)) {
            return;
        }

        WriteLine(std::cerr, message);
    });
#else
    rtc::InitLogger(rtc::LogLevel::Error, [](const rtc::LogLevel level, const rtc::string& message) {
        if (level == rtc::LogLevel::Error || level == rtc::LogLevel::Fatal) {
            AppendReleaseErrorLine(LogSeverity::Error, message);
        }
    });
#endif
}

/**
 * @brief 按级别写入日志行。
 * @param severity 日志严重级别。
 * @param line 待输出的文本内容。
 */
void WriteLogLine(const LogSeverity severity, const std::string_view line) {
#ifdef _DEBUG
    std::ostream& stream =
        (severity == LogSeverity::Error || severity == LogSeverity::Warning) ? std::cerr : std::cout;
    WriteLine(stream, line);
#else
    if (severity == LogSeverity::Error) {
        AppendReleaseErrorLine(severity, line);
    }
#endif
}

/**
 * @brief 写入Info行。
 * @param line 待输出的文本内容。
 */
void WriteInfoLine(const std::string_view line) {
    WriteLogLine(LogSeverity::Info, line);
}

/**
 * @brief 写入错误行。
 * @param line 待输出的文本内容。
 */
void WriteErrorLine(const std::string_view line) {
    WriteLogLine(LogSeverity::Error, line);
}

}  // namespace rdc::protocol::common
