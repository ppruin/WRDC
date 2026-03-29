/**
 * @file signaling_server.hpp
 * @brief 声明 server/signaling_gateway/signaling_server 相关的类型、函数与流程。
 */

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "../../protocol.hpp"
#include "../audit/server_logger.hpp"
#include "../transport/server_config.hpp"
#include "uwebsockets/App.h"

namespace rdc {

/**
 * @brief 封装 SignalingServer 相关的服务端流程。
 */
class SignalingServer {
public:
    /**
     * @brief 构造 SignalingServer 对象。
     * @param config 配置对象。
     */
    explicit SignalingServer(ServerConfig config);

    /**
     * @brief 运行相关流程。
     * @return 返回状态码或退出码。
     */
    int Run();

private:
    /**
     * @brief 定义 SessionState 的枚举取值。
     */
    enum class SessionState {
        PendingAccept,
        Signaling,
        Active,
        Closed,
        Failed
    };

    /**
     * @brief 描述 ConnectionContext 上下文信息。
     */
    struct ConnectionContext {
        std::string connection_id;
        std::string device_id;
        std::string user_id;
        std::string session_id;
        std::optional<protocol::ClientRole> role;
    };

    /**
     * @brief 描述单个信令连接的发送句柄。
     */
    struct ConnectionHandle {
        /**
         * @brief 执行 void 相关处理。
         * @return 返回对应结果。
         */
        std::function<void(const protocol::Json&)> send_json;
    };

    /**
     * @brief 描述 DeviceRecord 记录结构。
     */
    struct DeviceRecord {
        std::string device_id;
        std::string connection_id;
        /**
         * @brief 执行 object 相关处理。
         * @return 返回生成的 JSON 对象。
         */
        protocol::Json capabilities = protocol::Json::object();
        /**
         * @brief 执行 now 相关处理。
         * @return 返回对应结果。
         */
        std::chrono::steady_clock::time_point last_heartbeat = std::chrono::steady_clock::now();
    };

    /**
     * @brief 描述 SessionRecord 记录结构。
     */
    struct SessionRecord {
        std::string session_id;
        std::string controller_connection_id;
        std::string controller_user_id;
        std::string host_device_id;
        SessionState state = SessionState::PendingAccept;
    };

    ServerConfig config_;
    server::audit::ServerLogger logger_;
    std::atomic_uint64_t next_connection_id_{1};
    std::atomic_uint64_t next_session_id_{1};
    std::unordered_map<std::string, ConnectionHandle> connections_;
    std::unordered_map<std::string, DeviceRecord> devices_;
    std::unordered_map<std::string, std::string> controllers_;
    std::unordered_map<std::string, SessionRecord> sessions_;

    /**
     * @brief 创建连接Id。
     * @return 返回生成的字符串结果。
     */
    std::string CreateConnectionId();
    /**
     * @brief 创建会话Id。
     * @return 返回生成的字符串结果。
     */
    std::string CreateSessionId();
    /**
     * @brief 将输入值转换为字符串表示。
     * @param state 状态枚举值。
     * @return 返回生成的字符串结果。
     */
    static std::string ToString(SessionState state);
    /**
     * @brief 执行 Describe角色 相关处理。
     * @param ctx 连接或会话上下文。
     * @return 返回生成的字符串结果。
     */
    static std::string DescribeRole(const ConnectionContext& ctx);
    /**
     * @brief 执行 DispatchIf 相关处理。
     * @param actual_type 当前收到的消息类型。
     * @param expected_type 期望匹配的消息类型。
     * @param args args。
     * @return 返回是否成功或条件是否满足。
     */
    template <auto Handler, typename... Args>
    bool DispatchIf(std::string_view actual_type, std::string_view expected_type, Args&&... args);
    /**
     * @brief 执行 会话Belongs转换为 相关处理。
     * @param ctx 连接或会话上下文。
     * @param session 会话对象或会话记录。
     * @return 返回是否成功或条件是否满足。
     */
    template <protocol::ClientRole Role>
    static bool SessionBelongsTo(const ConnectionContext& ctx, const SessionRecord& session);
    /**
     * @brief 执行 会话Belongs转换为Any 相关处理。
     * @param ctx 连接或会话上下文。
     * @param session 会话对象或会话记录。
     * @return 返回是否成功或条件是否满足。
     */
    template <protocol::ClientRole... Roles>
    static bool SessionBelongsToAny(const ConnectionContext& ctx, const SessionRecord& session);
    /**
     * @brief 处理Text消息。
     * @param socket 当前套接字连接对象。
     * @param payload 协议负载数据。
     */
    template <typename Socket>
    void HandleTextMessage(Socket* socket, std::string_view payload);
    /**
     * @brief 处理注册设备。
     * @param socket 当前套接字连接对象。
     * @param ctx 连接或会话上下文。
     * @param message 待处理的消息对象。
     */
    template <typename Socket>
    void HandleRegisterDevice(Socket* socket, ConnectionContext& ctx, const protocol::Json& message);
    /**
     * @brief 处理注册控制端。
     * @param socket 当前套接字连接对象。
     * @param ctx 连接或会话上下文。
     * @param message 待处理的消息对象。
     */
    template <typename Socket>
    void HandleRegisterController(Socket* socket, ConnectionContext& ctx, const protocol::Json& message);
    /**
     * @brief 处理心跳。
     * @param socket 当前套接字连接对象。
     * @param ctx 连接或会话上下文。
     */
    template <typename Socket>
    void HandleHeartbeat(Socket* socket, ConnectionContext& ctx);
    /**
     * @brief 处理列表Devices。
     * @param socket 当前套接字连接对象。
     * @param ctx 连接或会话上下文。
     */
    template <typename Socket>
    void HandleListDevices(Socket* socket, const ConnectionContext& ctx) const;
    /**
     * @brief 处理创建会话。
     * @param socket 当前套接字连接对象。
     * @param ctx 连接或会话上下文。
     * @param message 待处理的消息对象。
     */
    template <typename Socket>
    void HandleCreateSession(Socket* socket, const ConnectionContext& ctx, const protocol::Json& message);
    /**
     * @brief 处理接受会话。
     * @param socket 当前套接字连接对象。
     * @param ctx 连接或会话上下文。
     * @param message 待处理的消息对象。
     */
    template <typename Socket>
    void HandleAcceptSession(Socket* socket, const ConnectionContext& ctx, const protocol::Json& message);
    /**
     * @brief 处理拒绝会话。
     * @param socket 当前套接字连接对象。
     * @param ctx 连接或会话上下文。
     * @param message 待处理的消息对象。
     */
    template <typename Socket>
    void HandleRejectSession(Socket* socket, const ConnectionContext& ctx, const protocol::Json& message);
    /**
     * @brief 处理信令。
     * @param socket 当前套接字连接对象。
     * @param ctx 连接或会话上下文。
     * @param message 待处理的消息对象。
     */
    template <typename Socket>
    void HandleSignal(Socket* socket, const ConnectionContext& ctx, const protocol::Json& message);
    /**
     * @brief 处理关闭会话。
     * @param socket 当前套接字连接对象。
     * @param ctx 连接或会话上下文。
     * @param message 待处理的消息对象。
     */
    template <typename Socket>
    void HandleCloseSession(Socket* socket, const ConnectionContext& ctx, const protocol::Json& message);

    /**
     * @brief 执行 On套接字打开 相关处理。
     * @param socket 当前套接字连接对象。
     */
    template <typename Socket>
    void OnSocketOpen(Socket* socket);
    /**
     * @brief 执行 On套接字关闭 相关处理。
     * @param socket 当前套接字连接对象。
     */
    template <typename Socket>
    void OnSocketClose(Socket* socket);

    /**
     * @brief 执行 FailSessionsFor连接 相关处理。
     * @param ctx 连接或会话上下文。
     * @param reason 状态变化原因。
     */
    void FailSessionsForConnection(const ConnectionContext& ctx, std::string_view reason);
    /**
     * @brief 发送会话状态。
     * @param session 会话对象或会话记录。
     * @param type 消息类型。
     * @param reason 状态变化原因。
     */
    void SendSessionState(const SessionRecord& session, std::string_view type, std::string_view reason = {}) const;
    /**
     * @brief 发送 JSON 消息。
     * @param socket 当前套接字连接对象。
     * @param message 待处理的消息对象。
     */
    template <typename Socket>
    void SendJson(Socket* socket, const protocol::Json& message) const;
    /**
     * @brief 发送错误。
     * @param socket 当前套接字连接对象。
     * @param code 错误码。
     * @param message 待处理的消息对象。
     */
    template <typename Socket>
    void SendError(Socket* socket, std::string_view code, std::string_view message) const;

    /**
     * @brief 查找连接。
     * @param connection_id 连接标识。
     * @return 返回对象指针或句柄。
     */
    ConnectionHandle* FindConnection(const std::string& connection_id);
    /**
     * @brief 查找连接。
     * @param connection_id 连接标识。
     * @return 返回对象指针或句柄。
     */
    const ConnectionHandle* FindConnection(const std::string& connection_id) const;
    /**
     * @brief 查找对等端。
     * @param session 会话对象或会话记录。
     * @param sender_role 发送方角色。
     * @return 返回对象指针或句柄。
     */
    const ConnectionHandle* FindPeer(const SessionRecord& session, protocol::ClientRole sender_role) const;
    /**
     * @brief 查找会话。
     * @param session_id 会话标识。
     * @return 返回对象指针或句柄。
     */
    SessionRecord* FindSession(const std::string& session_id);
    /**
     * @brief 查找会话。
     * @param session_id 会话标识。
     * @return 返回对象指针或句柄。
     */
    const SessionRecord* FindSession(const std::string& session_id) const;
};

/**
 * @brief 执行 DispatchIf 相关处理。
 * @param actual_type 当前收到的消息类型。
 * @param expected_type 期望匹配的消息类型。
 * @param args args。
 * @return 返回是否成功或条件是否满足。
 */
template <auto Handler, typename... Args>
bool SignalingServer::DispatchIf(const std::string_view actual_type,
                                 const std::string_view expected_type,
                                 Args&&... args) {
    if (actual_type != expected_type) {
        return false;
    }

    std::invoke(Handler, *this, std::forward<Args>(args)...);
    return true;
}

/**
 * @brief 执行 会话Belongs转换为 相关处理。
 * @param ctx 连接或会话上下文。
 * @param session 会话对象或会话记录。
 * @return 返回是否成功或条件是否满足。
 */
template <protocol::ClientRole Role>
bool SignalingServer::SessionBelongsTo(const ConnectionContext& ctx, const SessionRecord& session) {
    if (!ctx.role.has_value() || *ctx.role != Role) {
        return false;
    }

    if constexpr (Role == protocol::ClientRole::Host) {
        return session.host_device_id == ctx.device_id;
    } else {
        return session.controller_connection_id == ctx.connection_id;
    }
}

/**
 * @brief 执行 会话Belongs转换为Any 相关处理。
 * @param ctx 连接或会话上下文。
 * @param session 会话对象或会话记录。
 * @return 返回是否成功或条件是否满足。
 */
template <protocol::ClientRole... Roles>
bool SignalingServer::SessionBelongsToAny(const ConnectionContext& ctx, const SessionRecord& session) {
    return (SessionBelongsTo<Roles>(ctx, session) || ...);
}

}  // namespace rdc
