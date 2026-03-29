/**
 * @file host_client.hpp
 * @brief 声明 agent/session/host_client 相关的类型、函数与流程。
 */

#pragma once

#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "desktop_streamer.hpp"
#include "../rtc/peer_session.hpp"
#include "../../protocol.hpp"
#include "../../protocol/common/win_websocket_client.hpp"

namespace rdc {

/**
 * @brief 封装 HostClient 相关的客户端流程。
 */
class HostClient {
public:
    /**
     * @brief 构造 HostClient 对象。
     * @param signal_url 信令服务地址。
     * @param device_id 主机设备标识。
     */
    HostClient(std::string signal_url, std::string device_id);

    /**
     * @brief 运行相关流程。
     * @return 返回状态码或退出码。
     */
    int Run();

private:
    using Json = protocol::Json;
    /**
     * @brief 描述主机会话的运行时状态。
     */
    struct SessionRuntime {
        std::shared_ptr<PeerSession> peer_session;
    };

    /**
     * @brief 配置套接字。
     */
    void ConfigureSocket();
    /**
     * @brief 处理消息。
     * @param message 待处理的消息对象。
     */
    void HandleMessage(const Json& message);
    /**
     * @brief 运行桌面采集SmokeIfEnabled。
     * @return 返回是否成功或条件是否满足。
     */
    bool RunDesktopCaptureSmokeIfEnabled() const;
    /**
     * @brief 收集Active视频Sessions。
     * @return 返回结果集合。
     */
    std::vector<std::shared_ptr<PeerSession>> CollectActiveVideoSessions();
    /**
     * @brief 发送 JSON 消息。
     * @param message 待处理的消息对象。
     */
    void SendJson(const Json& message);
    /**
     * @brief 停止相关流程。
     */
    void Stop();

    std::string signal_url_;
    std::string device_id_;
    rdc::protocol::common::WinWebSocketClient signal_client_;
    std::unique_ptr<agent::session::DesktopStreamer> desktop_streamer_;
    std::unordered_map<std::string, SessionRuntime> sessions_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool stop_requested_ = false;
};

}  // namespace rdc
