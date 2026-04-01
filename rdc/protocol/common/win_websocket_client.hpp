/**
 * @file win_websocket_client.hpp
 * @brief 声明 protocol/common/win_websocket_client 相关的类型、函数与流程。
 */

#pragma once

#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include <windows.h>
#include <winhttp.h>

#include "../../protocol.hpp"

namespace rdc::protocol::common {

/**
 * @brief 封装 WinWebSocketClient 相关的 WinHTTP WebSocket 客户端流程。
 */
class WinWebSocketClient {
public:
    using Json = protocol::Json;

    /**
     * @brief 定义 WebSocket 连接关闭时触发的回调类型。
     */
    using ClosedHandler = std::function<void()>;

    /**
     * @brief 定义 WebSocket 连接出错时触发的回调类型。
     */
    using ErrorHandler = std::function<void(const std::string&)>;

/**
     * @brief 定义收到 WebSocket JSON 消息时触发的回调类型。
     */
    using MessageHandler = std::function<void(const Json&)>;

    /**
     * @brief 构造 WinWebSocketClient 对象。
     */
    WinWebSocketClient();
    /**
     * @brief 析构 WinWebSocketClient 对象并释放相关资源。
     */
    ~WinWebSocketClient();

    /**
     * @brief 构造 WinWebSocketClient 对象。
     */
    WinWebSocketClient(const WinWebSocketClient&) = delete;
    WinWebSocketClient& operator=(const WinWebSocketClient&) = delete;

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
     * @brief 发送 JSON 消息。
     * @param message 待处理的消息对象。
     */
    void SendJson(const Json& message);
    /**
     * @brief 关闭相关流程。
     */
    void Close();

private:
    /**
     * @brief 描述解析后的 WebSocket 地址信息。
     */
    struct ParsedUrl {
        bool secure = false;
        std::wstring host;
        INTERNET_PORT port = INTERNET_DEFAULT_HTTP_PORT;
        std::wstring path = L"/";
    };

    /**
     * @brief 解析Url。
     * @param url 目标地址。
     * @return 返回对应结果。
     */
    static ParsedUrl ParseUrl(const std::string& url);
    /**
     * @brief 将输入值转换为Wide表示。
     * @param value 输入值。
     * @return 返回生成的宽字符串结果。
     */
    static std::wstring ToWide(const std::string& value);

    /**
     * @brief 执行 `ReceiveLoop` 对应的处理逻辑。
     */
    void ReceiveLoop();
    /**
     * @brief 关闭Handles。
     */
    void CloseHandles();
    /**
     * @brief 上报错误信息。
     * @param message 待处理的消息对象。
     */
    void ReportError(const std::string& message) const;

    mutable std::mutex mutex_;
    HINTERNET session_handle_ = nullptr;
    HINTERNET connection_handle_ = nullptr;
    HINTERNET request_handle_ = nullptr;
    HINTERNET websocket_handle_ = nullptr;
    std::thread receive_thread_;
    bool stop_requested_ = false;
    ClosedHandler closed_handler_;
    ErrorHandler error_handler_;
    MessageHandler message_handler_;
};

}  // namespace rdc::protocol::common
