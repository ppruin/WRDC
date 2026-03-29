/**
 * @file peer_session.hpp
 * @brief 声明 agent/rtc/peer_session 相关的类型、函数与流程。
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "rtc/rtc.h"

#include "../encoder/encoded_video_frame.hpp"
#include "../../protocol.hpp"

namespace rdc {

/**
 * @brief 定义 PeerSession 的角色取值。
 */
enum class PeerRole {
    Host,
    Controller
};

/**
 * @brief 封装 PeerSession 相关的对等连接、数据通道与视频轨协商流程。
 */
class PeerSession {
public:
    using Json = protocol::Json;

    /**
     * @brief 定义用于发送信令 JSON 的回调类型。
     */
    using SignalSender = std::function<void(const Json&)>;

    /**
     * @brief 定义处理控制通道消息的回调类型。
     */
    using MessageHandler = std::function<void(std::string_view channel_label, const Json& payload)>;

/**
     * @brief 定义处理收到的视频样本数据的回调类型。
     */
    using VideoSampleHandler = std::function<void(const std::uint8_t* data, std::size_t size)>;

    /**
     * @brief 构造 PeerSession 对象。
     * @param role 角色枚举值。
     * @param session_id 会话标识。
     * @param signal_sender 用于发送信令消息的回调对象。
     * @param message_handler 用于处理消息的回调对象。
     * @param video_sample_handler 用于接收视频样本数据的回调对象。
     */
    PeerSession(PeerRole role,

                std::string session_id,

                SignalSender signal_sender,

                MessageHandler message_handler,

                VideoSampleHandler video_sample_handler = {});

    /**
     * @brief 启动相关流程。
     */
    void Start();

    /**
     * @brief 处理信令。
     * @param payload 协议负载数据。
     */
    void HandleSignal(const Json& payload);

    /**
     * @brief 发送控制。
     * @param payload 协议负载数据。
     */
    void SendControl(const Json& payload);

    /**
     * @brief 发送视频帧。
     * @param frame 视频帧对象。
     */
    void SendVideoFrame(const agent::encoder::EncodedVideoFrame& frame);

    /**
     * @brief 判断视频Ready是否满足条件。
     * @return 返回是否成功或条件是否满足。
     */
    bool IsVideoReady() const;

    /**
     * @brief 消费PendingKeyframeRequest。
     * @return 返回是否成功或条件是否满足。
     */
    bool ConsumePendingKeyframeRequest();

    /**
     * @brief 关闭相关流程。
     */
    void Close();

private:

    /**
     * @brief 确保对等端连接已就绪。
     */
    void EnsurePeerConnection();

    /**
     * @brief 确保视频轨道已就绪。
     */
    void EnsureVideoTrack();

    /**
     * @brief 处理Remote描述。
     * @param payload 协议负载数据。
     */
    void HandleRemoteDescription(const Json& payload);

    /**
     * @brief 处理Remote候选项。
     * @param payload 协议负载数据。
     */
    void HandleRemoteCandidate(const Json& payload);

    /**
     * @brief 刷新PendingCandidates。
     */
    void FlushPendingCandidates();

    /**
     * @brief 将Pending候选项加入队列。
     * @param payload 协议负载数据。
     */
    void QueuePendingCandidate(const Json& payload);

    /**
     * @brief 发送信令。
     * @param payload 协议负载数据。
     */
    void SendSignal(const Json& payload) const;

    /**
     * @brief 记录相关流程。
     * @param message 待处理的消息对象。
     */
    void Log(const std::string& message) const;

    /**
     * @brief 配置数据Channel。
     * @param data_channel_id 数据通道标识。
     */
    void ConfigureDataChannel(int data_channel_id);

    /**
     * @brief 配置轨道。
     * @param track_id 媒体轨标识。
     */
    void ConfigureTrack(int track_id);

    /**
     * @brief 处理数据Channel消息。
     * @param channel_label 数据通道标签。
     * @param payload 协议负载数据。
     * @param size 字节长度。
     */
    void HandleDataChannelMessage(std::string_view channel_label, const char* payload, std::size_t size);

    /**
     * @brief 处理轨道消息。
     * @param track_id 媒体轨标识。
     * @param message 待处理的消息对象。
     * @param size 字节长度。
     */
    void HandleTrackMessage(int track_id, const char* message, std::size_t size);

    /**
     * @brief 计算Initial视频Ssrc。
     * @param role 角色枚举值。
     * @param session_id 会话标识。
     * @return 返回计算得到的数值结果。
     */
    static std::uint32_t ComputeInitialVideoSsrc(PeerRole role, std::string_view session_id);

    /**
     * @brief 计算RTP时间戳Delta。
     * @param sample_duration_hns 样本时长，单位为 100ns。
     * @return 返回计算得到的数值结果。
     */
    static std::uint32_t ComputeRtpTimestampDelta(std::int64_t sample_duration_hns);

    /**
     * @brief 将连接状态转换为可读字符串。
     * @param state 状态枚举值。
     * @return 返回生成的字符串结果。
     */
    static std::string StateToString(rtcState state);

    /**
     * @brief 处理Local描述。
     * @param pc pc。
     * @param sdp sdp。
     * @param type 消息类型。
     * @param ptr ptr。
     */
    static void RTC_API HandleLocalDescription(int pc, const char* sdp, const char* type, void* ptr);

    /**
     * @brief 处理Local候选项。
     * @param pc pc。
     * @param cand cand。
     * @param mid mid。
     * @param ptr ptr。
     */
    static void RTC_API HandleLocalCandidate(int pc, const char* cand, const char* mid, void* ptr);

    /**
     * @brief 处理状态Change。
     * @param pc pc。
     * @param state 状态枚举值。
     * @param ptr ptr。
     */
    static void RTC_API HandleStateChange(int pc, rtcState state, void* ptr);

    /**
     * @brief 处理数据Channel。
     * @param pc pc。
     * @param dc dc。
     * @param ptr ptr。
     */
    static void RTC_API HandleDataChannel(int pc, int dc, void* ptr);

    /**
     * @brief 处理轨道。
     * @param pc pc。
     * @param tr tr。
     * @param ptr ptr。
     */
    static void RTC_API HandleTrack(int pc, int tr, void* ptr);

    /**
     * @brief 处理Channel打开。
     * @param id id。
     * @param ptr ptr。
     */
    static void RTC_API HandleChannelOpen(int id, void* ptr);

    /**
     * @brief 处理ChannelClosed。
     * @param id id。
     * @param ptr ptr。
     */
    static void RTC_API HandleChannelClosed(int id, void* ptr);

    /**
     * @brief 处理Channel错误。
     * @param id id。
     * @param error error。
     * @param ptr ptr。
     */
    static void RTC_API HandleChannelError(int id, const char* error, void* ptr);

    /**
     * @brief 处理Channel消息。
     * @param id id。
     * @param message 待处理的消息对象。
     * @param size 字节长度。
     * @param ptr ptr。
     */
    static void RTC_API HandleChannelMessage(int id, const char* message, int size, void* ptr);

    /**
     * @brief 处理轨道打开。
     * @param id id。
     * @param ptr ptr。
     */
    static void RTC_API HandleTrackOpen(int id, void* ptr);

    /**
     * @brief 处理轨道Closed。
     * @param id id。
     * @param ptr ptr。
     */
    static void RTC_API HandleTrackClosed(int id, void* ptr);

    /**
     * @brief 处理轨道错误。
     * @param id id。
     * @param error error。
     * @param ptr ptr。
     */
    static void RTC_API HandleTrackError(int id, const char* error, void* ptr);

    /**
     * @brief 处理轨道消息Callback。
     * @param id id。
     * @param message 待处理的消息对象。
     * @param size 字节长度。
     * @param ptr ptr。
     */
    static void RTC_API HandleTrackMessageCallback(int id, const char* message, int size, void* ptr);

    /**
     * @brief 处理视频Pli。
     * @param tr tr。
     * @param ptr ptr。
     */
    static void RTC_API HandleVideoPli(int tr, void* ptr);

    /**
     * @brief 获取数据ChannelLabel。
     * @param data_channel_id 数据通道标识。
     * @return 返回生成的字符串结果。
     */
    static std::string GetDataChannelLabel(int data_channel_id);

    /**
     * @brief 获取轨道Mid。
     * @param track_id 媒体轨标识。
     * @return 返回生成的字符串结果。
     */
    static std::string GetTrackMid(int track_id);

    template <typename Func>

    /**
     * @brief 在持有会话互斥锁的情况下执行给定回调。
     * @param func 需要在锁保护下执行的回调对象。
     * @return 返回回调执行后的结果。
     */
    decltype(auto) WithLock(Func&& func);

    template <typename Func>

    /**
     * @brief 在持有会话互斥锁的情况下执行给定只读回调。
     * @param func 需要在锁保护下执行的只读回调对象。
     * @return 返回回调执行后的结果。
     */
    decltype(auto) WithLock(Func&& func) const;

    PeerRole role_;

    std::string session_id_;

    SignalSender signal_sender_;

    MessageHandler message_handler_;

    VideoSampleHandler video_sample_handler_;

    int peer_connection_id_ = -1;

    int control_channel_id_ = -1;

    int video_track_id_ = -1;

    bool started_ = false;

    bool remote_description_set_ = false;

    bool video_track_open_ = false;

    bool pending_video_keyframe_request_ = false;

    bool peer_connected_ = false;

    bool logged_first_keyframe_profile_ = false;

    std::uint32_t video_rtp_timestamp_ = 0;

    std::uint64_t sent_video_frames_ = 0;

    std::uint64_t received_video_samples_ = 0;

    std::string control_channel_label_ = "control";

    std::string video_track_mid_ = "video";

    int negotiated_video_payload_type_ = 102;

    std::string negotiated_video_profile_ = "profile-level-id=42e01f;packetization-mode=1;level-asymmetry-allowed=1";

    mutable std::recursive_mutex mutex_;

    std::vector<Json> pending_candidates_;

};

template <typename Func>

/**
 * @brief 在持有会话互斥锁的情况下执行给定回调。
 * @param func 需要在锁保护下执行的回调对象。
 * @return 返回回调执行后的结果。
 */
decltype(auto) PeerSession::WithLock(Func&& func) {
    std::scoped_lock lock(mutex_);

    return std::forward<Func>(func)();
}

template <typename Func>

/**
 * @brief 在持有会话互斥锁的情况下执行给定只读回调。
 * @param func 需要在锁保护下执行的只读回调对象。
 * @return 返回回调执行后的结果。
 */
decltype(auto) PeerSession::WithLock(Func&& func) const {
    std::scoped_lock lock(mutex_);

    return std::forward<Func>(func)();
}

}  // namespace rdc
