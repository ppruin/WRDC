/**
 * @file server_logger.cpp
 * @brief 实现 server/audit/server_logger 相关的类型、函数与流程。
 */

#include "server_logger.hpp"

#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <system_error>

#include "../../protocol/common/console_logger.hpp"

namespace rdc::server::audit {

/**
 * @brief 执行 服务端日志器 相关处理。
 * @param config 配置对象。
 */
ServerLogger::ServerLogger(const ServerConfig& config)
    : config_(config) {
#ifdef _DEBUG
    if (config_.save_logs) {
        InitializeFileSink();
    }
#endif
}

/**
 * @brief 执行 Info 相关处理。
 * @param message 待处理的消息对象。
 */
void ServerLogger::Info(const std::string_view message) const {
    Write(ServerLogLevel::Info, message);
}

/**
 * @brief 执行 Warning 相关处理。
 * @param message 待处理的消息对象。
 */
void ServerLogger::Warning(const std::string_view message) const {
    Write(ServerLogLevel::Warning, message);
}

/**
 * @brief 执行 错误 相关处理。
 * @param message 待处理的消息对象。
 */
void ServerLogger::Error(const std::string_view message) const {
    Write(ServerLogLevel::Error, message);
}

/**
 * @brief 执行 Debug 相关处理。
 * @param message 待处理的消息对象。
 */
void ServerLogger::Debug(const std::string_view message) const {
    if (IsVerbose()) {
        Write(ServerLogLevel::Debug, message);
    }
}

/**
 * @brief 判断Verbose是否满足条件。
 * @return 返回是否成功或条件是否满足。
 */
bool ServerLogger::IsVerbose() const {
    return config_.log_verbosity == ServerLogVerbosity::Verbose;
}

/**
 * @brief 写入相关流程。
 * @param level 日志级别。
 * @param message 待处理的消息对象。
 */
void ServerLogger::Write(const ServerLogLevel level, const std::string_view message) const {
#ifdef _DEBUG
    const std::string line = "[" + BuildTimestamp() + "][" + ToChineseLevel(level) + "] " + std::string(message);

    std::scoped_lock lock(mutex_);
    std::ostream& stream = level == ServerLogLevel::Error ? std::cerr : std::cout;
    stream << line << '\n';

    if (file_stream_.is_open()) {
        file_stream_ << line << '\n';
        file_stream_.flush();
    }
#else
    if (level == ServerLogLevel::Error) {
        protocol::common::WriteLogLine(protocol::common::LogSeverity::Error, message);
    }
#endif
}

/**
 * @brief 初始化文件Sink。
 */
void ServerLogger::InitializeFileSink() {
    std::filesystem::path path = config_.log_file_path.empty()
                                     ? std::filesystem::path(BuildDefaultLogFilePath())
                                     : std::filesystem::path(config_.log_file_path);

    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
    }

    file_stream_.open(path, std::ios::out | std::ios::app | std::ios::binary);
    if (!file_stream_.is_open()) {
        std::cerr << "[日志][错误] 无法打开日志文件: " << path.string() << '\n';
        return;
    }

    config_.log_file_path = path.string();
}

/**
 * @brief 构建时间戳。
 * @return 返回生成的字符串结果。
 */
std::string ServerLogger::BuildTimestamp() {
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
 * @brief 将输入值转换为Chinese级别表示。
 * @param level 日志级别。
 * @return 返回对象指针或句柄。
 */
const char* ServerLogger::ToChineseLevel(const ServerLogLevel level) {
    switch (level) {
    case ServerLogLevel::Info:
        return "信息";
    case ServerLogLevel::Warning:
        return "警告";
    case ServerLogLevel::Error:
        return "错误";
    case ServerLogLevel::Debug:
        return "详细";
    }

    return "日志";
}

/**
 * @brief 构建默认日志文件路径。
 * @return 返回生成的字符串结果。
 */
std::string ServerLogger::BuildDefaultLogFilePath() {
    const auto now = std::chrono::system_clock::now();
    const auto current_time = std::chrono::system_clock::to_time_t(now);

    std::tm local_time{};
#ifdef _WIN32
    localtime_s(&local_time, &current_time);
#else
    localtime_r(&current_time, &local_time);
#endif

    std::ostringstream oss;
    oss << "logs/server-" << std::put_time(&local_time, "%Y%m%d-%H%M%S") << ".log";
    return oss.str();
}

}  // namespace rdc::server::audit
