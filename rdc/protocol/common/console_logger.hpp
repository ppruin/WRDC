/**
 * @file console_logger.hpp
 * @brief 声明 protocol/common/console_logger 相关的类型、函数与流程。
 */

#pragma once

#include <string_view>

namespace rdc::protocol::common {

/**
 * @brief 定义日志严重级别。
 */
enum class LogSeverity {
    Info,
    Warning,
    Error,
    Debug
};

/**
 * @brief 初始化控制台日志器。
 */
void InitializeConsoleLogger();
/**
 * @brief 按级别写入日志行。
 * @param severity 日志严重级别。
 * @param line 待输出的文本内容。
 */
void WriteLogLine(LogSeverity severity, std::string_view line);
/**
 * @brief 写入Info行。
 * @param line 待输出的文本内容。
 */
void WriteInfoLine(std::string_view line);
/**
 * @brief 写入错误行。
 * @param line 待输出的文本内容。
 */
void WriteErrorLine(std::string_view line);

}  // namespace rdc::protocol::common
