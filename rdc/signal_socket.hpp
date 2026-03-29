/**
 * @file signal_socket.hpp
 * @brief 声明 signal_socket 相关的类型、函数与流程。
 */

#pragma once

#include <chrono>
#include <functional>
#include <mutex>
#include <string>

#include "rtc/rtc.h"

#include "protocol.hpp"

namespace rdc {

/**
 * @brief 封装 SignalSocket 相关的信令 WebSocket 连接流程。
 */
class SignalSocket {
public:
    using Json = protocol::Json;

    /**
     * @brief 定义信令连接建立时触发的回调类型。
     */
    using OpenHandler = std::function<void()>;

    /**
     * @brief 定义信令连接关闭时触发的回调类型。
     */
    using ClosedHandler = std::function<void()>;

    /**
     * @brief 定义信令连接出错时触发的回调类型。
     */
    using ErrorHandler = std::function<void(const std::string&)>;

/**
     * @brief 定义收到信令消息时触发的回调类型。
     */
    using MessageHandler = std::function<void(const Json&)>;

    /**
     * @brief 构造 SignalSocket 对象。
     */
    SignalSocket();
    /**
     * @brief 析构 SignalSocket 对象并释放相关资源。
     */
    ~SignalSocket();

    /**
     * @brief 构造 SignalSocket 对象。
     */
    SignalSocket(const SignalSocket&) = delete;
    SignalSocket& operator=(const SignalSocket&) = delete;

    /**
     * @brief 设置连接建立回调。
     * @param handler 回调对象。
     */
    void SetOpenHandler(OpenHandler handler);
    /**
     * @brief 设置关闭回调。
     * @param handler 回调对象。
     */
    void SetClosedHandler(ClosedHandler handler);
    /**
     * @brief 设置错误回调。
     * @param handler 回调对象。
     */
    void SetErrorHandler(ErrorHandler handler);
    /**
     * @brief 设置消息回调。
     * @param handler 回调对象。
     */
    void SetMessageHandler(MessageHandler handler);

    /**
     * @brief 连接到指定地址。
     * @param url 目标地址。
     */
    void Connect(const std::string& url);
    /**
     * @brief 等待Until打开。
     * @param timeout 等待超时时间。
     * @return 返回是否成功或条件是否满足。
     */
    bool WaitUntilOpen(std::chrono::milliseconds timeout);
    /**
     * @brief 发送相关流程。
     * @param message 待处理的消息对象。
     */
    void Send(const Json& message);
    /**
     * @brief 关闭相关流程。
     */
    void Close();

private:
    /**
     * @brief 处理打开。
     * @param id id。
     * @param ptr ptr。
     */
    static void RTC_API HandleOpen(int id, void* ptr);
    /**
     * @brief 处理Closed。
     * @param id id。
     * @param ptr ptr。
     */
    static void RTC_API HandleClosed(int id, void* ptr);
    /**
     * @brief 处理错误。
     * @param id id。
     * @param error error。
     * @param ptr ptr。
     */
    static void RTC_API HandleError(int id, const char* error, void* ptr);
    /**
     * @brief 处理消息。
     * @param id id。
     * @param message 待处理的消息对象。
     * @param size 字节长度。
     * @param ptr ptr。
     */
    static void RTC_API HandleMessage(int id, const char* message, int size, void* ptr);

    int socket_id_ = -1;
    std::mutex mutex_;
    OpenHandler open_handler_;
    ClosedHandler closed_handler_;
    ErrorHandler error_handler_;
    MessageHandler message_handler_;
};

}  // namespace rdc
