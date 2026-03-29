/**
 * @file server_logger.hpp
 * @brief 声明 server/audit/server_logger 相关的类型、函数与流程。
 */

#pragma once

#include <fstream>
#include <mutex>
#include <string>
#include <string_view>

#include "../transport/server_config.hpp"

namespace rdc::server::audit {

/**
 * @brief 定义 ServerLogLevel 的枚举取值。
 */
enum class ServerLogLevel {
    Info,
    Warning,
    Error,
    Debug
};

/**
 * @brief 封装 ServerLogger 相关的日志输出能力。
 */
class ServerLogger {
public:
    /**
     * @brief 执行 服务端日志器 相关处理。
     * @param config 配置对象。
     */
    explicit ServerLogger(const ServerConfig& config);

    /**
     * @brief 执行 Info 相关处理。
     * @param message 待处理的消息对象。
     */
    void Info(std::string_view message) const;
    /**
     * @brief 执行 Warning 相关处理。
     * @param message 待处理的消息对象。
     */
    void Warning(std::string_view message) const;
    /**
     * @brief 执行 错误 相关处理。
     * @param message 待处理的消息对象。
     */
    void Error(std::string_view message) const;
    /**
     * @brief 执行 Debug 相关处理。
     * @param message 待处理的消息对象。
     */
    void Debug(std::string_view message) const;
    /**
     * @brief 判断Verbose是否满足条件。
     * @return 返回是否成功或条件是否满足。
     */
    bool IsVerbose() const;

private:
    /**
     * @brief 写入相关流程。
     * @param level 日志级别。
     * @param message 待处理的消息对象。
     */
    void Write(ServerLogLevel level, std::string_view message) const;
    /**
     * @brief 初始化文件Sink。
     */
    void InitializeFileSink();
    /**
     * @brief 构建时间戳。
     * @return 返回生成的字符串结果。
     */
    static std::string BuildTimestamp();
    /**
     * @brief 将输入值转换为Chinese级别表示。
     * @param level 日志级别。
     * @return 返回对象指针或句柄。
     */
    static const char* ToChineseLevel(ServerLogLevel level);
    /**
     * @brief 构建默认日志文件路径。
     * @return 返回生成的字符串结果。
     */
    static std::string BuildDefaultLogFilePath();

    ServerConfig config_;
    mutable std::mutex mutex_;
    mutable std::ofstream file_stream_;
};

}  // namespace rdc::server::audit
