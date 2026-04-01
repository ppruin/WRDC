/**
 * @file host_client.hpp
 * @brief 声明 agent/session/host_client 相关的类型、函数与流程。
 */

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
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
     * @brief 描述捕获输出在虚拟桌面中的区域。
     */
    struct DesktopOutputBounds {
        int left = 0;
        int top = 0;
        int width = 0;
        int height = 0;

        bool IsValid() const {
            return width > 0 && height > 0;
        }
    };

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
     * @brief 处理控制通道消息。
     * @param session_id 会话标识。
     * @param channel_label 数据通道标签。
     * @param payload 协议负载数据。
     */
    void HandleControlMessage(const std::string& session_id, std::string_view channel_label, const Json& payload);
    /**
     * @brief 处理经信令通道转发的控制消息。
     * @param session_id 会话标识。
     * @param signal_payload 信令负载对象。
     * @return 返回是否已识别并处理控制消息。
     */
    bool HandleSignaledControlMessage(const std::string& session_id, const Json& signal_payload);
    /**
     * @brief 同步远端鼠标位置。
     * @param normalized_x 归一化横坐标。
     * @param normalized_y 归一化纵坐标。
     * @return 返回是否成功或条件是否满足。
     */
    bool SyncRemoteMousePosition(double normalized_x, double normalized_y) const;
    /**
     * @brief 启动输入派发线程组。
     */
    void StartInputDispatchers();
    /**
     * @brief 停止输入派发线程组。
     */
    void StopInputDispatchers();
    /**
     * @brief 将输入控制消息加入派发队列。
     * @param payload 协议负载数据。
     */
    void QueueInputControl(const Json& payload);
    /**
     * @brief 将键盘控制消息加入派发队列。
     * @param payload 协议负载数据。
     */
    void QueueKeyboardInputControl(const Json& payload);
    /**
     * @brief 将鼠标控制消息加入派发队列。
     * @param payload 协议负载数据。
     */
    void QueueMouseInputControl(const Json& payload);
    /**
     * @brief 运行输入派发循环。
     */
    void RunInputLoop();
    /**
     * @brief 在键盘输入线程中应用排队的远端输入事件。
     * @param payload 协议负载数据。
     */
    void ApplyQueuedKeyboardInput(const Json& payload);
    /**
     * @brief 在鼠标输入线程中应用排队的鼠标坐标同步事件。
     * @param payload 协议负载数据。
     */
    void ApplyQueuedMouseMoveInput(const Json& payload);
    /**
     * @brief 在鼠标输入线程中应用排队的鼠标按键事件。
     * @param payload 协议负载数据。
     */
    void ApplyQueuedMouseButtonInput(const Json& payload);
    /**
     * @brief 在鼠标输入线程中应用排队的鼠标滚轮事件。
     * @param payload 协议负载数据。
     */
    void ApplyQueuedMouseWheelInput(const Json& payload);
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
    DesktopOutputBounds desktop_output_bounds_;
    std::uint32_t desktop_output_index_ = 0;
    bool desktop_output_bounds_ready_ = false;
    std::mutex input_control_mutex_;
    std::condition_variable input_control_cv_;
    std::deque<Json> pending_input_controls_;
    std::thread input_thread_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic_bool stop_requested_{false};
};

}  // namespace rdc
