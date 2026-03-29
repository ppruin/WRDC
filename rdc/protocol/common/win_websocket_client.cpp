/**
 * @file win_websocket_client.cpp
 * @brief 实现 protocol/common/win_websocket_client 相关的类型、函数与流程。
 */

#include "win_websocket_client.hpp"

#include <algorithm>
#include <stdexcept>
#include <vector>

namespace rdc::protocol::common {

namespace {

/**
 * @brief 将字符串转换为小写形式。
 * @param value 输入值。
 * @return 返回生成的字符串结果。
 */
std::string Lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

/**
 * @brief 执行 WinWeb套接字客户端 相关处理。
 */
}  // namespace

WinWebSocketClient::WinWebSocketClient() = default;

/**
 * @brief 析构 WinWebSocketClient 对象并释放相关资源。
 */
WinWebSocketClient::~WinWebSocketClient() {
    Close();
}

/**
 * @brief 设置关闭回调。
 * @param handler 回调对象。
 */
void WinWebSocketClient::SetClosedHandler(ClosedHandler handler) {
    closed_handler_ = std::move(handler);
}

/**
 * @brief 设置错误回调。
 * @param handler 回调对象。
 */
void WinWebSocketClient::SetErrorHandler(ErrorHandler handler) {
    error_handler_ = std::move(handler);
}

/**
 * @brief 设置消息回调。
 * @param handler 回调对象。
 */
void WinWebSocketClient::SetMessageHandler(MessageHandler handler) {
    message_handler_ = std::move(handler);
}

/**
 * @brief 连接到指定地址。
 * @param url 目标地址。
 */
void WinWebSocketClient::Connect(const std::string& url) {
    Close();

    const auto parsed = ParseUrl(url);

    std::scoped_lock lock(mutex_);
    stop_requested_ = false;

    session_handle_ = WinHttpOpen(L"rdc/1.0",
                                  WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                  WINHTTP_NO_PROXY_NAME,
                                  WINHTTP_NO_PROXY_BYPASS,
                                  0);
    if (session_handle_ == nullptr) {
        throw std::runtime_error("WinHttpOpen 调用失败");
    }

    connection_handle_ = WinHttpConnect(session_handle_, parsed.host.c_str(), parsed.port, 0);
    if (connection_handle_ == nullptr) {
        CloseHandles();
        throw std::runtime_error("WinHttpConnect 调用失败");
    }

    const DWORD request_flags = parsed.secure ? WINHTTP_FLAG_SECURE : 0;
    request_handle_ = WinHttpOpenRequest(connection_handle_,
                                         L"GET",
                                         parsed.path.c_str(),
                                         nullptr,
                                         WINHTTP_NO_REFERER,
                                         WINHTTP_DEFAULT_ACCEPT_TYPES,
                                         request_flags);
    if (request_handle_ == nullptr) {
        CloseHandles();
        throw std::runtime_error("WinHttpOpenRequest 调用失败");
    }

    if (!WinHttpSetOption(request_handle_, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0)) {
        CloseHandles();
        throw std::runtime_error("设置 WinHTTP WebSocket 升级选项失败");
    }

    if (!WinHttpSendRequest(request_handle_,
                            WINHTTP_NO_ADDITIONAL_HEADERS,
                            0,
                            WINHTTP_NO_REQUEST_DATA,
                            0,
                            0,
                            0)) {
        CloseHandles();
        throw std::runtime_error("WinHttpSendRequest 调用失败");
    }

    if (!WinHttpReceiveResponse(request_handle_, nullptr)) {
        CloseHandles();
        throw std::runtime_error("WinHttpReceiveResponse 调用失败");
    }

    websocket_handle_ = WinHttpWebSocketCompleteUpgrade(request_handle_, 0);
    if (websocket_handle_ == nullptr) {
        CloseHandles();
        throw std::runtime_error("WinHttpWebSocketCompleteUpgrade 调用失败");
    }

    WinHttpCloseHandle(request_handle_);
    request_handle_ = nullptr;

    receive_thread_ = std::thread([this]() {
        ReceiveLoop();
    });
}

/**
 * @brief 发送 JSON 消息。
 * @param message 待处理的消息对象。
 */
void WinWebSocketClient::SendJson(const Json& message) {
    std::string payload = message.dump();

    std::scoped_lock lock(mutex_);
    if (websocket_handle_ == nullptr) {
        throw std::runtime_error("WebSocket 句柄尚未打开");
    }

    const DWORD status = WinHttpWebSocketSend(websocket_handle_,
                                              WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
                                              payload.data(),
                                              static_cast<DWORD>(payload.size()));
    if (status != ERROR_SUCCESS) {
        throw std::runtime_error("WinHttpWebSocketSend 调用失败");
    }
}

/**
 * @brief 关闭相关流程。
 */
void WinWebSocketClient::Close() {
    {
        std::scoped_lock lock(mutex_);
        stop_requested_ = true;

        if (websocket_handle_ != nullptr) {
            WinHttpWebSocketClose(websocket_handle_,
                                  WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS,
                                  nullptr,
                                  0);
        }
    }

    if (receive_thread_.joinable()) {
        receive_thread_.join();
    }

    std::scoped_lock lock(mutex_);
    CloseHandles();
}

/**
 * @brief 解析Url。
 * @param url 目标地址。
 * @return 返回对应结果。
 */
WinWebSocketClient::ParsedUrl WinWebSocketClient::ParseUrl(const std::string& url) {
    const auto scheme_end = url.find("://");
    if (scheme_end == std::string::npos) {
        throw std::runtime_error("WebSocket URL 必须包含协议头");
    }

    const std::string scheme = Lowercase(url.substr(0, scheme_end));
    const bool secure = scheme == "wss";
    if (!secure && scheme != "ws") {
        throw std::runtime_error("WebSocket URL 必须以 ws:// 或 wss:// 开头");
    }

    const std::string remainder = url.substr(scheme_end + 3);
    const auto slash = remainder.find('/');
    const std::string authority = slash == std::string::npos ? remainder : remainder.substr(0, slash);
    const std::string path = slash == std::string::npos ? "/" : remainder.substr(slash);

    const auto colon = authority.rfind(':');
    std::string host = authority;
    INTERNET_PORT port = secure ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;
    if (colon != std::string::npos) {
        host = authority.substr(0, colon);
        port = static_cast<INTERNET_PORT>(std::stoi(authority.substr(colon + 1)));
    }

    if (host.empty()) {
        throw std::runtime_error("WebSocket URL 缺少主机地址");
    }

    return ParsedUrl{
        .secure = secure,
        .host = ToWide(host),
        .port = port,
        .path = ToWide(path)
    };
}

/**
 * @brief 将输入值转换为Wide表示。
 * @param value 输入值。
 * @return 返回生成的字符串结果。
 */
std::wstring WinWebSocketClient::ToWide(const std::string& value) {
    if (value.empty()) {
        return {};
    }

    const int length = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0);
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), result.data(), length);
    return result;
}

/**
 * @brief 执行 `ReceiveLoop` 对应的处理逻辑。
 */
void WinWebSocketClient::ReceiveLoop() {
    std::string assembled_message;
    std::vector<char> buffer(4096);

    for (;;) {
        HINTERNET websocket = nullptr;
        bool should_stop = false;

        {
            std::scoped_lock lock(mutex_);
            websocket = websocket_handle_;
            should_stop = stop_requested_;
        }

        if (should_stop || websocket == nullptr) {
            return;
        }

        DWORD bytes_read = 0;
        WINHTTP_WEB_SOCKET_BUFFER_TYPE buffer_type = WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE;
        const DWORD status = WinHttpWebSocketReceive(websocket,
                                                     buffer.data(),
                                                     static_cast<DWORD>(buffer.size()),
                                                     &bytes_read,
                                                     &buffer_type);

        if (status != ERROR_SUCCESS) {
            {
                std::scoped_lock lock(mutex_);
                if (stop_requested_) {
                    return;
                }
            }

            ReportError("WinHttpWebSocketReceive 调用失败，状态码=" + std::to_string(status));
            return;
        }

        if (buffer_type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) {
            if (closed_handler_) {
                closed_handler_();
            }
            return;
        }

        if (buffer_type == WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE ||
            buffer_type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) {
            assembled_message.append(buffer.data(), buffer.data() + bytes_read);

            if (buffer_type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) {
                const auto json = Json::parse(assembled_message, nullptr, false);
                assembled_message.clear();

                if (!json.is_discarded() && json.is_object() && message_handler_) {
                    message_handler_(json);
                }
            }
        }
    }
}

/**
 * @brief 关闭Handles。
 */
void WinWebSocketClient::CloseHandles() {
    if (websocket_handle_ != nullptr) {
        WinHttpCloseHandle(websocket_handle_);
        websocket_handle_ = nullptr;
    }

    if (request_handle_ != nullptr) {
        WinHttpCloseHandle(request_handle_);
        request_handle_ = nullptr;
    }

    if (connection_handle_ != nullptr) {
        WinHttpCloseHandle(connection_handle_);
        connection_handle_ = nullptr;
    }

    if (session_handle_ != nullptr) {
        WinHttpCloseHandle(session_handle_);
        session_handle_ = nullptr;
    }
}

/**
 * @brief 上报错误信息。
 * @param message 待处理的消息对象。
 */
void WinWebSocketClient::ReportError(const std::string& message) const {
    if (error_handler_) {
        error_handler_(message);
    }
}

}  // namespace rdc::protocol::common
