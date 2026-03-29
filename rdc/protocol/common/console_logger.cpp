/**
 * @file console_logger.cpp
 * @brief 实现 protocol/common/console_logger 相关的类型、函数与流程。
 */

#include "console_logger.hpp"

#include <iostream>
#include <mutex>
#include <string_view>

#include <rtc/global.hpp>

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
 * @brief 初始化控制台日志器。
 */
}  // namespace

void InitializeConsoleLogger() {
    rtc::InitLogger(rtc::LogLevel::Warning, [](const rtc::LogLevel level, const rtc::string& message) {
        if (ShouldSuppressRtcWarning(level, message)) {
            return;
        }

        WriteLine(std::cerr, message);
    });
}

/**
 * @brief 写入Info行。
 * @param line 待输出的文本内容。
 */
void WriteInfoLine(const std::string_view line) {
    WriteLine(std::cout, line);
}

/**
 * @brief 写入错误行。
 * @param line 待输出的文本内容。
 */
void WriteErrorLine(const std::string_view line) {
    WriteLine(std::cerr, line);
}

}  // namespace rdc::protocol::common
