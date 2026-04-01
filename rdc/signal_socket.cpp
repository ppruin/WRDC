/**
 * @file signal_socket.cpp
 * @brief 实现 signal_socket 相关的类型、函数与流程。
 */

#include "signal_socket.hpp"

#include <chrono>
#include <stdexcept>
#include <thread>
#include <utility>

namespace rdc {

/**
 * @brief 构造 SignalSocket 对象。
 */
SignalSocket::SignalSocket() = default;

/**
 * @brief 析构 SignalSocket 对象并释放相关资源。
 */
SignalSocket::~SignalSocket() {
    Close();
}

/**
 * @brief 设置连接建立回调。
 * @param handler 回调对象。
 */
void SignalSocket::SetOpenHandler(OpenHandler handler) {
    open_handler_ = std::move(handler);
}

/**
 * @brief 设置关闭回调。
 * @param handler 回调对象。
 */
void SignalSocket::SetClosedHandler(ClosedHandler handler) {
    closed_handler_ = std::move(handler);
}

/**
 * @brief 设置错误回调。
 * @param handler 回调对象。
 */
void SignalSocket::SetErrorHandler(ErrorHandler handler) {
    error_handler_ = std::move(handler);
}

/**
 * @brief 设置消息回调。
 * @param handler 回调对象。
 */
void SignalSocket::SetMessageHandler(MessageHandler handler) {
    message_handler_ = std::move(handler);
}

/**
 * @brief 连接到指定地址。
 * @param url 目标地址。
 */
void SignalSocket::Connect(const std::string& url) {
    std::scoped_lock lock(mutex_);

    if (socket_id_ >= 0) {
        throw std::runtime_error("信令套接字已处于连接状态");
    }

    socket_id_ = rtcCreateWebSocket(url.c_str());
    if (socket_id_ < 0) {
        socket_id_ = -1;
        throw std::runtime_error("创建 WebSocket 失败");
    }

    rtcSetUserPointer(socket_id_, this);
    rtcSetOpenCallback(socket_id_, &SignalSocket::HandleOpen);
    rtcSetClosedCallback(socket_id_, &SignalSocket::HandleClosed);
    rtcSetErrorCallback(socket_id_, &SignalSocket::HandleError);
    rtcSetMessageCallback(socket_id_, &SignalSocket::HandleMessage);

    if (rtcIsOpen(socket_id_) && open_handler_) {
        open_handler_();
    }
}

/**
 * @brief 发送相关流程。
 * @param message 待处理的消息对象。
 */
void SignalSocket::Send(const Json& message) {
    std::scoped_lock lock(mutex_);

    if (socket_id_ < 0) {
        throw std::runtime_error("信令套接字尚未连接");
    }

    const std::string payload = message.dump();
    if (rtcSendMessage(socket_id_, payload.c_str(), static_cast<int>(payload.size())) != RTC_ERR_SUCCESS) {
        throw std::runtime_error("发送信令消息失败");
    }
}

/**
 * @brief 等待Until打开。
 * @param timeout 等待超时时间。
 * @return 返回是否成功或条件是否满足。
 */
bool SignalSocket::WaitUntilOpen(std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;

    for (;;) {
        {
            std::scoped_lock lock(mutex_);
            if (socket_id_ >= 0 && rtcIsOpen(socket_id_)) {
                return true;
            }
        }

        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

/**
 * @brief 关闭相关流程。
 */
void SignalSocket::Close() {
    std::scoped_lock lock(mutex_);

    if (socket_id_ < 0) {
        return;
    }

    rtcDeleteWebSocket(socket_id_);
    socket_id_ = -1;
}

/**
 * @brief 处理打开。
 * @param int int。
 * @param ptr ptr。
 */
void RTC_API SignalSocket::HandleOpen(int /*id*/, void* ptr) {
    auto* self = static_cast<SignalSocket*>(ptr);
    if (self != nullptr && self->open_handler_) {
        self->open_handler_();
    }
}

/**
 * @brief 处理Closed。
 * @param int int。
 * @param ptr ptr。
 */
void RTC_API SignalSocket::HandleClosed(int /*id*/, void* ptr) {
    auto* self = static_cast<SignalSocket*>(ptr);
    if (self != nullptr && self->closed_handler_) {
        self->closed_handler_();
    }
}

/**
 * @brief 处理错误。
 * @param int int。
 * @param error error。
 * @param ptr ptr。
 */
void RTC_API SignalSocket::HandleError(int /*id*/, const char* error, void* ptr) {
    auto* self = static_cast<SignalSocket*>(ptr);
    if (self != nullptr && self->error_handler_) {
        self->error_handler_(error != nullptr ? error : "未知 WebSocket 错误");
    }
}

/**
 * @brief 处理消息。
 * @param int int。
 * @param message 待处理的消息对象。
 * @param size 字节长度。
 * @param ptr ptr。
 */
void RTC_API SignalSocket::HandleMessage(int /*id*/, const char* message, int size, void* ptr) {
    auto* self = static_cast<SignalSocket*>(ptr);
    if (self == nullptr || !self->message_handler_) {
        return;
    }

    const std::string payload(message, message + size);
    const auto json = Json::parse(payload, nullptr, false);
    if (!json.is_discarded() && json.is_object()) {
        self->message_handler_(json);
    }
}

}  // namespace rdc
