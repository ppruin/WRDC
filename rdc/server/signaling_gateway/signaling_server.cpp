/**
 * @file signaling_server.cpp
 * @brief 实现 server/signaling_gateway/signaling_server 相关的类型、函数与流程。
 */

#include "signaling_server.hpp"

#include <string>
#include <type_traits>
#include <utility>

#include "../../controller/ui/browser_controller_assets.hpp"
#include "../../protocol/common/json_utils.hpp"

namespace rdc {

namespace {

/**
 * @brief 获取ObjectOrEmpty。
 * @param message 待处理的消息对象。
 * @param key key。
 * @return 返回生成的 JSON 对象。
 */
const protocol::Json& GetObjectOrEmpty(const protocol::Json& message, const char* key) {
    static const protocol::Json kEmptyObject = protocol::Json::object();

    const auto it = message.find(key);
    if (it == message.end() || !it->is_object()) {
        return kEmptyObject;
    }

    return *it;
}

/**
 * @brief 写入TextResponse。
 * @param response HTTP 响应对象。
 * @param content_type HTTP 响应的内容类型。
 * @param body 响应正文内容。
 */
template <typename Response>
void WriteTextResponse(Response* response, const std::string_view content_type, const std::string_view body) {
    response->writeHeader("Content-Type", std::string(content_type));
    response->writeHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    response->writeHeader("Pragma", "no-cache");
    response->writeHeader("Expires", "0");
    response->end(std::string(body));
}

}  // namespace

/**
 * @brief 构造 SignalingServer 对象。
 * @param config 配置对象。
 */
SignalingServer::SignalingServer(ServerConfig config)
    : config_(std::move(config)),
      logger_(config_) {
}

/**
 * @brief 发送 JSON 消息。
 * @param socket 当前套接字连接对象。
 * @param message 待处理的消息对象。
 */
template <typename Socket>
void SignalingServer::SendJson(Socket* socket, const protocol::Json& message) const {
    if (socket == nullptr) {
        return;
    }

    socket->send(message.dump(), uWS::OpCode::TEXT);
}

/**
 * @brief 发送错误。
 * @param socket 当前套接字连接对象。
 * @param code 错误码。
 * @param message 待处理的消息对象。
 */
template <typename Socket>
void SignalingServer::SendError(Socket* socket, std::string_view code, std::string_view message) const {
    logger_.Warning("请求处理失败: code=" + std::string(code) + ", message=" + std::string(message));
    SendJson(socket, protocol::MakeError(code, message));
}

/**
 * @brief 处理注册设备。
 * @param socket 当前套接字连接对象。
 * @param ctx 连接或会话上下文。
 * @param message 待处理的消息对象。
 */
template <typename Socket>
void SignalingServer::HandleRegisterDevice(Socket* socket, ConnectionContext& ctx, const protocol::Json& message) {
    const auto* device_id = protocol::common::FindString(message, "deviceId");
    if (device_id == nullptr || device_id->empty()) {
        SendError(socket, "missing_device_id", "register_device requires deviceId");
        return;
    }

    ctx.role = protocol::ClientRole::Host;
    ctx.device_id = *device_id;
    ctx.user_id.clear();

    auto& record = devices_[*device_id];
    record.device_id = *device_id;
    record.connection_id = ctx.connection_id;
    record.capabilities = GetObjectOrEmpty(message, "capabilities");
    record.last_heartbeat = std::chrono::steady_clock::now();

    SendJson(socket,
             protocol::Json{
                 {"type", "registered"},
                 {"role", protocol::ToString(*ctx.role)},
                 {"deviceId", *device_id},
                 {"connectionId", ctx.connection_id}
             });

    logger_.Info("设备已注册: deviceId=" + *device_id + ", connectionId=" + ctx.connection_id);
}

/**
 * @brief 处理注册控制端。
 * @param socket 当前套接字连接对象。
 * @param ctx 连接或会话上下文。
 * @param message 待处理的消息对象。
 */
template <typename Socket>
void SignalingServer::HandleRegisterController(Socket* socket, ConnectionContext& ctx, const protocol::Json& message) {
    const auto* user_id = protocol::common::FindString(message, "userId");
    if (user_id == nullptr || user_id->empty()) {
        SendError(socket, "missing_user_id", "register_controller requires userId");
        return;
    }

    ctx.role = protocol::ClientRole::Controller;
    ctx.user_id = *user_id;
    ctx.device_id.clear();

    controllers_[*user_id] = ctx.connection_id;

    SendJson(socket,
             protocol::Json{
                 {"type", "registered"},
                 {"role", protocol::ToString(*ctx.role)},
                 {"userId", *user_id},
                 {"connectionId", ctx.connection_id}
             });

    logger_.Info("控制端已注册: userId=" + *user_id + ", connectionId=" + ctx.connection_id);
}

/**
 * @brief 处理心跳。
 * @param socket 当前套接字连接对象。
 * @param ctx 连接或会话上下文。
 */
template <typename Socket>
void SignalingServer::HandleHeartbeat(Socket* socket, ConnectionContext& ctx) {
    if (ctx.role == protocol::ClientRole::Host && !ctx.device_id.empty()) {
        if (auto* record = protocol::common::FindMapped(devices_, ctx.device_id); record != nullptr) {
            record->last_heartbeat = std::chrono::steady_clock::now();
        }
    }

    SendJson(socket, protocol::Json{{"type", "heartbeat_ack"}});
}

/**
 * @brief 处理列表Devices。
 * @param socket 当前套接字连接对象。
 * @param ctx 连接或会话上下文。
 */
template <typename Socket>
void SignalingServer::HandleListDevices(Socket* socket, const ConnectionContext& ctx) const {
    if (ctx.role != protocol::ClientRole::Controller) {
        SendError(socket, "forbidden", "only controllers may list devices");
        return;
    }

    protocol::Json items = protocol::Json::array();
    for (const auto& [device_id, record] : devices_) {
        items.push_back(protocol::Json{
            {"deviceId", device_id},
            {"state", "online"},
            {"capabilities", record.capabilities}
        });
    }

    SendJson(socket,
             protocol::Json{
                 {"type", "device_list"},
                 {"onlineDeviceCount", items.size()},
                 {"devices", std::move(items)}
             });
}

/**
 * @brief 处理创建会话。
 * @param socket 当前套接字连接对象。
 * @param ctx 连接或会话上下文。
 * @param message 待处理的消息对象。
 */
template <typename Socket>
void SignalingServer::HandleCreateSession(Socket* socket, const ConnectionContext& ctx, const protocol::Json& message) {
    if (ctx.role != protocol::ClientRole::Controller) {
        SendError(socket, "forbidden", "only controllers may create sessions");
        return;
    }

    const auto* target_device_id = protocol::common::FindString(message, "targetDeviceId");
    if (target_device_id == nullptr || target_device_id->empty()) {
        SendError(socket, "missing_target_device", "create_session requires targetDeviceId");
        return;
    }

    const auto* device = protocol::common::FindMapped(devices_, *target_device_id);
    if (device == nullptr) {
        SendError(socket, "device_offline", "target device is not online");
        return;
    }

    SessionRecord session;
    session.session_id = CreateSessionId();
    session.controller_connection_id = ctx.connection_id;
    session.controller_user_id = ctx.user_id;
    session.host_device_id = *target_device_id;
    session.state = SessionState::PendingAccept;

    sessions_.emplace(session.session_id, session);

    logger_.Info("已创建会话: sessionId=" + session.session_id +
                 ", userId=" + ctx.user_id +
                 ", targetDeviceId=" + *target_device_id);

    SendJson(socket,
             protocol::Json{
                 {"type", "session_created"},
                 {"sessionId", session.session_id},
                 {"state", ToString(session.state)},
                 {"targetDeviceId", *target_device_id}
             });

    if (const auto* host_connection = FindConnection(device->connection_id); host_connection != nullptr) {
        host_connection->send_json(protocol::Json{
            {"type", "session_request"},
            {"sessionId", session.session_id},
            {"controllerUserId", ctx.user_id},
            {"targetDeviceId", *target_device_id}
        });
    }
}

/**
 * @brief 处理接受会话。
 * @param socket 当前套接字连接对象。
 * @param ctx 连接或会话上下文。
 * @param message 待处理的消息对象。
 */
template <typename Socket>
void SignalingServer::HandleAcceptSession(Socket* socket, const ConnectionContext& ctx, const protocol::Json& message) {
    if (ctx.role != protocol::ClientRole::Host) {
        SendError(socket, "forbidden", "only hosts may accept sessions");
        return;
    }

    const auto* session_id = protocol::common::FindString(message, "sessionId");
    auto* session = session_id != nullptr ? FindSession(*session_id) : nullptr;
    if (session == nullptr) {
        SendError(socket, "session_not_found", "unknown sessionId");
        return;
    }

    if (!SessionBelongsTo<protocol::ClientRole::Host>(ctx, *session)) {
        SendError(socket, "forbidden", "session does not belong to this host");
        return;
    }

    session->state = SessionState::Signaling;
    SendSessionState(*session, "session_accepted");
    logger_.Info("会话已接受: sessionId=" + *session_id + ", hostDeviceId=" + ctx.device_id);
}

/**
 * @brief 处理拒绝会话。
 * @param socket 当前套接字连接对象。
 * @param ctx 连接或会话上下文。
 * @param message 待处理的消息对象。
 */
template <typename Socket>
void SignalingServer::HandleRejectSession(Socket* socket, const ConnectionContext& ctx, const protocol::Json& message) {
    if (ctx.role != protocol::ClientRole::Host) {
        SendError(socket, "forbidden", "only hosts may reject sessions");
        return;
    }

    const auto* session_id = protocol::common::FindString(message, "sessionId");
    const auto reason = protocol::common::GetStringViewOr(message, "reason", "rejected_by_host");
    auto* session = session_id != nullptr ? FindSession(*session_id) : nullptr;
    if (session == nullptr) {
        SendError(socket, "session_not_found", "unknown sessionId");
        return;
    }

    if (!SessionBelongsTo<protocol::ClientRole::Host>(ctx, *session)) {
        SendError(socket, "forbidden", "session does not belong to this host");
        return;
    }

    session->state = SessionState::Closed;
    SendSessionState(*session, "session_rejected", reason);
    logger_.Warning("会话已拒绝: sessionId=" + *session_id + ", reason=" + std::string(reason));
}

/**
 * @brief 处理信令。
 * @param socket 当前套接字连接对象。
 * @param ctx 连接或会话上下文。
 * @param message 待处理的消息对象。
 */
template <typename Socket>
void SignalingServer::HandleSignal(Socket* socket, const ConnectionContext& ctx, const protocol::Json& message) {
    const auto* session_id = protocol::common::FindString(message, "sessionId");
    auto* session = session_id != nullptr ? FindSession(*session_id) : nullptr;
    if (session == nullptr) {
        SendError(socket, "session_not_found", "unknown sessionId");
        return;
    }

    if (!SessionBelongsToAny<protocol::ClientRole::Host, protocol::ClientRole::Controller>(ctx, *session)) {
        SendError(socket, "forbidden", "session does not belong to this connection");
        return;
    }

    const auto* peer_connection = FindPeer(*session, *ctx.role);
    if (peer_connection == nullptr) {
        SendError(socket, "peer_offline", "peer connection is no longer available");
        session->state = SessionState::Failed;
        return;
    }

    if (session->state == SessionState::Signaling) {
        session->state = SessionState::Active;
        logger_.Info("会话进入活动状态: sessionId=" + *session_id);
    }

    peer_connection->send_json(protocol::Json{
        {"type", "signal"},
        {"sessionId", *session_id},
        {"fromRole", protocol::ToString(*ctx.role)},
        {"payload", GetObjectOrEmpty(message, "payload")}
    });
}

/**
 * @brief 处理关闭会话。
 * @param socket 当前套接字连接对象。
 * @param ctx 连接或会话上下文。
 * @param message 待处理的消息对象。
 */
template <typename Socket>
void SignalingServer::HandleCloseSession(Socket* socket, const ConnectionContext& ctx, const protocol::Json& message) {
    const auto* session_id = protocol::common::FindString(message, "sessionId");
    auto* session = session_id != nullptr ? FindSession(*session_id) : nullptr;
    if (session == nullptr) {
        SendError(socket, "session_not_found", "unknown sessionId");
        return;
    }

    if (!SessionBelongsToAny<protocol::ClientRole::Host, protocol::ClientRole::Controller>(ctx, *session)) {
        SendError(socket, "forbidden", "session does not belong to this connection");
        return;
    }

    const auto reason = protocol::common::GetStringViewOr(message, "reason", "closed_by_peer");
    session->state = SessionState::Closed;
    SendSessionState(*session, "session_closed", reason);
    logger_.Info("会话已关闭: sessionId=" + *session_id + ", reason=" + std::string(reason));
}

/**
 * @brief 处理Text消息。
 * @param socket 当前套接字连接对象。
 * @param payload 协议负载数据。
 */
template <typename Socket>
void SignalingServer::HandleTextMessage(Socket* socket, std::string_view payload) {
    auto* ctx = socket->getUserData();

    const auto message = protocol::Json::parse(payload, nullptr, false);
    if (message.is_discarded() || !message.is_object()) {
        SendError(socket, "invalid_json", "message must be a valid JSON object");
        return;
    }

    const auto type = protocol::common::GetStringViewOr(message, "type");
    logger_.Debug("收到消息: connectionId=" + ctx->connection_id + ", role=" + DescribeRole(*ctx) + ", type=" + std::string(type));

    if (type == protocol::kRegisterDevice) {
        HandleRegisterDevice(socket, *ctx, message);
        return;
    }

    if (type == protocol::kRegisterController) {
        HandleRegisterController(socket, *ctx, message);
        return;
    }

    if (!ctx->role.has_value()) {
        SendError(socket, "registration_required", "register as host or controller before sending other messages");
        return;
    }

    if (type == protocol::kHeartbeat) {
        HandleHeartbeat(socket, *ctx);
        return;
    }

    if (type == protocol::kListDevices) {
        HandleListDevices(socket, *ctx);
        return;
    }

    if (type == protocol::kCreateSession) {
        HandleCreateSession(socket, *ctx, message);
        return;
    }

    if (type == protocol::kAcceptSession) {
        HandleAcceptSession(socket, *ctx, message);
        return;
    }

    if (type == protocol::kRejectSession) {
        HandleRejectSession(socket, *ctx, message);
        return;
    }

    if (type == protocol::kSignal) {
        HandleSignal(socket, *ctx, message);
        return;
    }

    if (type == protocol::kCloseSession) {
        HandleCloseSession(socket, *ctx, message);
        return;
    }

    SendError(socket, "unsupported_type", "message type is not supported");
}

/**
 * @brief 执行 On套接字打开 相关处理。
 * @param socket 当前套接字连接对象。
 */
template <typename Socket>
void SignalingServer::OnSocketOpen(Socket* socket) {
    auto* ctx = socket->getUserData();
    ctx->connection_id = CreateConnectionId();
    connections_[ctx->connection_id] = ConnectionHandle{
        [socket](const protocol::Json& message) {
            socket->send(message.dump(), uWS::OpCode::TEXT);
        }
    };

    logger_.Info("新连接已建立: connectionId=" + ctx->connection_id);

    SendJson(socket,
             protocol::Json{
                 {"type", "hello"},
                 {"connectionId", ctx->connection_id},
                 {"message", "register as host or controller to begin"}
             });
}

/**
 * @brief 执行 On套接字关闭 相关处理。
 * @param socket 当前套接字连接对象。
 */
template <typename Socket>
void SignalingServer::OnSocketClose(Socket* socket) {
    const auto ctx = *socket->getUserData();
    connections_.erase(ctx.connection_id);

    logger_.Info("连接已关闭: connectionId=" + ctx.connection_id + ", role=" + DescribeRole(ctx));

    if (ctx.role == protocol::ClientRole::Host && !ctx.device_id.empty()) {
        devices_.erase(ctx.device_id);
    }

    if (ctx.role == protocol::ClientRole::Controller && !ctx.user_id.empty()) {
        if (const auto* controller_connection = protocol::common::FindMapped(controllers_, ctx.user_id);
            controller_connection != nullptr && *controller_connection == ctx.connection_id) {
            controllers_.erase(ctx.user_id);
        }
    }

    FailSessionsForConnection(ctx, "peer_disconnected");
}

/**
 * @brief 运行相关流程。
 * @return 返回状态码或退出码。
 */
int SignalingServer::Run() {
    constexpr int kMaxPayloadLength = 64 * 1024;

    logger_.Info("信令服务正在启动");
    logger_.Info("监听地址: " + config_.bind_host + ":" + std::to_string(config_.signal_port));
    logger_.Info(std::string("日志级别: ") + (logger_.IsVerbose() ? "详细" : "简略"));
    logger_.Info(std::string("服务协议: ") + (config_.enable_tls ? "HTTPS/WSS" : "HTTP/WS"));
    if (config_.save_logs) {
        logger_.Info("日志文件: " + config_.log_file_path);
    }
    if (config_.enable_tls) {
        logger_.Info("TLS 证书: " + config_.tls_cert_path);
        logger_.Info("TLS 私钥: " + config_.tls_key_path);
    }

    auto configure_routes = [this](auto& app) {
        app.get("/", [this](auto* response, auto* /*request*/) {
            WriteTextResponse(response,
                              "text/html; charset=utf-8",
                              controller::ui::GetBrowserControllerHtml());
        });

        app.get("/index.html", [this](auto* response, auto* /*request*/) {
            WriteTextResponse(response,
                              "text/html; charset=utf-8",
                              controller::ui::GetBrowserControllerHtml());
        });

        app.get("/controller.js", [this](auto* response, auto* /*request*/) {
            WriteTextResponse(response,
                              "application/javascript; charset=utf-8",
                              controller::ui::GetBrowserControllerScript());
        });

        app.get("/healthz", [this](auto* response, auto* /*request*/) {
            protocol::Json body{
                {"status", "ok"},
                {"service", "rdc-signaling"},
                {"onlineDevices", devices_.size()},
                {"openSessions", sessions_.size()}
            };

            response->writeHeader("Content-Type", "application/json; charset=utf-8");
            response->end(body.dump());
        });

        using AppType = std::remove_reference_t<decltype(app)>;
        constexpr bool kIsSslApp = std::is_same_v<AppType, uWS::SSLApp>;
        using SocketType = uWS::WebSocket<kIsSslApp, true, ConnectionContext>;
        using BehaviorType = typename uWS::TemplatedApp<kIsSslApp>::template WebSocketBehavior<ConnectionContext>;
        BehaviorType behavior{};
        behavior.compression = uWS::SHARED_COMPRESSOR;
        behavior.maxPayloadLength = kMaxPayloadLength;
        behavior.idleTimeout = 30;
        behavior.open = [this](SocketType* socket) {
            OnSocketOpen(socket);
        };
        behavior.message = [this](SocketType* socket, std::string_view message, uWS::OpCode op_code) {
            if (op_code != uWS::OpCode::TEXT) {
                SendError(socket, "unsupported_opcode", "signal endpoint only accepts text frames");
                return;
            }

            HandleTextMessage(socket, message);
        };
        behavior.close = [this](SocketType* socket, int /*code*/, std::string_view /*message*/) {
            OnSocketClose(socket);
        };

        app.ws<ConnectionContext>("/signal", std::move(behavior));
    };

    auto start_listen = [this](auto& app) {
        bool listen_failed = true;
        auto listen_handler = [this, &listen_failed](auto* listen_socket) {
            if (listen_socket == nullptr) {
                logger_.Error("端口监听失败: " + std::to_string(config_.signal_port));
                return;
            }

            listen_failed = false;
            logger_.Info("信令服务已启动");
            logger_.Info(std::string("浏览器入口: ") + (config_.enable_tls ? "https://" : "http://") +
                         "<主机IP>:" + std::to_string(config_.signal_port) + "/");
        };

        if (config_.bind_host.empty()) {
            app.listen(config_.signal_port, std::move(listen_handler));
        } else {
            app.listen(config_.bind_host, config_.signal_port, std::move(listen_handler));
        }

        return !listen_failed;
    };

    if (config_.enable_tls) {
        uWS::SocketContextOptions options{};
        options.cert_file_name = config_.tls_cert_path.c_str();
        options.key_file_name = config_.tls_key_path.c_str();
        if (!config_.tls_ca_path.empty()) {
            options.ca_file_name = config_.tls_ca_path.c_str();
        }

        uWS::SSLApp app(options);
        configure_routes(app);
        if (!start_listen(app)) {
            return 1;
        }

        app.run();
        return 0;
    }

    uWS::App app;
    configure_routes(app);
    if (!start_listen(app)) {
        return 1;
    }

    app.run();
    return 0;
}

/**
 * @brief 创建连接Id。
 * @return 返回生成的字符串结果。
 */
std::string SignalingServer::CreateConnectionId() {
    return "conn_" + std::to_string(next_connection_id_.fetch_add(1));
}

/**
 * @brief 创建会话Id。
 * @return 返回生成的字符串结果。
 */
std::string SignalingServer::CreateSessionId() {
    return "sess_" + std::to_string(next_session_id_.fetch_add(1));
}

/**
 * @brief 将输入值转换为字符串表示。
 * @param state 状态枚举值。
 * @return 返回生成的字符串结果。
 */
std::string SignalingServer::ToString(const SessionState state) {
    switch (state) {
    case SessionState::PendingAccept:
        return "pending_accept";
    case SessionState::Signaling:
        return "signaling";
    case SessionState::Active:
        return "active";
    case SessionState::Closed:
        return "closed";
    case SessionState::Failed:
        return "failed";
    }

    return "unknown";
}

/**
 * @brief 执行 Describe角色 相关处理。
 * @param ctx 连接或会话上下文。
 * @return 返回生成的字符串结果。
 */
std::string SignalingServer::DescribeRole(const ConnectionContext& ctx) {
    if (!ctx.role.has_value()) {
        return "未注册";
    }

    return std::string(protocol::ToString(*ctx.role));
}

/**
 * @brief 执行 FailSessionsFor连接 相关处理。
 * @param ctx 连接或会话上下文。
 * @param reason 状态变化原因。
 */
void SignalingServer::FailSessionsForConnection(const ConnectionContext& ctx, std::string_view reason) {
    std::vector<std::string> affected_sessions;
    affected_sessions.reserve(sessions_.size());

    for (const auto& [session_id, session] : sessions_) {
        if (SessionBelongsToAny<protocol::ClientRole::Host, protocol::ClientRole::Controller>(ctx, session) &&
            session.state != SessionState::Closed &&
            session.state != SessionState::Failed) {
            affected_sessions.push_back(session_id);
        }
    }

    for (const auto& session_id : affected_sessions) {
        if (auto* session = FindSession(session_id); session != nullptr) {
            session->state = SessionState::Failed;
            SendSessionState(*session, "session_failed", reason);
            logger_.Warning("会话失败: sessionId=" + session_id + ", reason=" + std::string(reason));
        }
    }
}

/**
 * @brief 发送会话状态。
 * @param session 会话对象或会话记录。
 * @param type 消息类型。
 * @param reason 状态变化原因。
 */
void SignalingServer::SendSessionState(const SessionRecord& session, std::string_view type, std::string_view reason) const {
    protocol::Json message{
        {"type", type},
        {"sessionId", session.session_id},
        {"state", ToString(session.state)},
        {"hostDeviceId", session.host_device_id}
    };

    if (!reason.empty()) {
        message["reason"] = reason;
    }

    if (const auto* controller_connection = FindConnection(session.controller_connection_id); controller_connection != nullptr) {
        controller_connection->send_json(message);
    }

    if (const auto* device = protocol::common::FindMapped(devices_, session.host_device_id); device != nullptr) {
        if (const auto* host_connection = FindConnection(device->connection_id); host_connection != nullptr) {
            host_connection->send_json(message);
        }
    }
}

/**
 * @brief 查找连接。
 * @param connection_id 连接标识。
 * @return 返回对象指针或句柄。
 */
SignalingServer::ConnectionHandle* SignalingServer::FindConnection(const std::string& connection_id) {
    return protocol::common::FindMapped(connections_, connection_id);
}

/**
 * @brief 查找连接。
 * @param connection_id 连接标识。
 * @return 返回对象指针或句柄。
 */
const SignalingServer::ConnectionHandle* SignalingServer::FindConnection(const std::string& connection_id) const {
    return protocol::common::FindMapped(connections_, connection_id);
}

/**
 * @brief 查找对等端。
 * @param session 会话对象或会话记录。
 * @param sender_role 发送方角色。
 * @return 返回对象指针或句柄。
 */
const SignalingServer::ConnectionHandle* SignalingServer::FindPeer(const SessionRecord& session,
                                                                   protocol::ClientRole sender_role) const {
    if (sender_role == protocol::ClientRole::Controller) {
        const auto* device = protocol::common::FindMapped(devices_, session.host_device_id);
        if (device == nullptr) {
            return nullptr;
        }

        return FindConnection(device->connection_id);
    }

    return FindConnection(session.controller_connection_id);
}

/**
 * @brief 查找会话。
 * @param session_id 会话标识。
 * @return 返回对象指针或句柄。
 */
SignalingServer::SessionRecord* SignalingServer::FindSession(const std::string& session_id) {
    return protocol::common::FindMapped(sessions_, session_id);
}

/**
 * @brief 查找会话。
 * @param session_id 会话标识。
 * @return 返回对象指针或句柄。
 */
const SignalingServer::SessionRecord* SignalingServer::FindSession(const std::string& session_id) const {
    return protocol::common::FindMapped(sessions_, session_id);
}

}  // namespace rdc
