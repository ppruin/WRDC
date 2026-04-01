/**
 * @file controller_client.hpp
 * @brief 声明 controller/rtc/controller_client 相关的类型、函数与流程。
 */

#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "h264_rtp_depacketizer.hpp"
#include "../../agent/rtc/peer_session.hpp"
#include "../../protocol.hpp"
#include "../../protocol/common/win_websocket_client.hpp"

namespace rdc {

/**
 * @brief 封装 ControllerClient 相关的客户端流程。
 */
class ControllerClient {
public:
    /**
     * @brief 构造 ControllerClient 对象。
     * @param signal_url 信令服务地址。
     * @param user_id 控制端用户标识。
     * @param target_device_id 目标主机设备标识。
     */
    ControllerClient(std::string signal_url, std::string user_id, std::string target_device_id);

    /**
     * @brief 运行相关流程。
     * @return 返回状态码或退出码。
     */
    int Run();

private:
    using Json = protocol::Json;
    using AnnexBNalu = controller::rtc::H264RtpDepacketizer::AnnexBNalu;

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
     * @brief 构建访问单元。
     * @param data 输入数据或缓冲区指针。
     * @param size 字节长度。
     * @return 返回可用结果；失败时返回空值。
     */
    std::optional<AnnexBNalu> BuildAccessUnit(const std::uint8_t* data, std::size_t size);
    /**
     * @brief 处理视频样本。
     * @param data 输入数据或缓冲区指针。
     * @param size 字节长度。
     */
    void HandleVideoSample(const std::uint8_t* data, std::size_t size);
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
    std::string user_id_;
    std::string target_device_id_;
    rdc::protocol::common::WinWebSocketClient signal_client_;
    std::shared_ptr<PeerSession> session_;
    controller::rtc::H264RtpDepacketizer video_depacketizer_;
    AnnexBNalu pending_access_unit_;
    std::string session_id_;
    std::atomic_bool first_video_access_unit_received_{false};
    std::atomic_uint64_t received_video_access_units_{0};
    std::mutex video_packet_mutex_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool stop_requested_ = false;
};

}  // namespace rdc
