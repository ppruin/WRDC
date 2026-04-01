/**
 * @file peer_session.cpp
 * @brief 实现 agent/rtc/peer_session 相关的类型、函数与流程。
 */

#include "peer_session.hpp"

#include <algorithm>

#include <array>

#include <condition_variable>

#include <cctype>

#include <cstdio>

#include <cstdint>

#include <ctime>

#include <memory>

#include <optional>

#include <stdexcept>

#include <string>

#include <string_view>

#include <thread>

#include <utility>

#include <vector>

#include "../../protocol/common/console_logger.hpp"

namespace rdc {

namespace {

constexpr char kVideoMid[] = "video";

constexpr char kControlChannelLabel[] = "control";

constexpr char kRealtimeControlChannelLabel[] = "control_rt";

constexpr char kDefaultTrackName[] = "desktop";

constexpr char kDefaultMsid[] = "rdc";

constexpr char kDefaultTrackId[] = "desktop-video";

constexpr std::uint32_t kVideoClockRate = 90'000;

constexpr int kDefaultH264PayloadType = 102;

constexpr char kDefaultH264Profile[] = "profile-level-id=42e01f;packetization-mode=1;level-asymmetry-allowed=1";

constexpr std::uint32_t kDefaultRtpTimestampDelta = 3'000;

/**
 * @brief 描述 NegotiatedVideoConfig 的配置项。
 */
struct NegotiatedVideoConfig {

    int payload_type = kDefaultH264PayloadType;

    std::string profile = kDefaultH264Profile;

};

/**
 * @brief 判断字符串是否以前缀开头。
 * @param value 输入值。
 * @param prefix prefix。
 * @return 返回是否成功或条件是否满足。
 */
bool StartsWith(const std::string_view value, const std::string_view prefix) {

    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;

}

/**
 * @brief 去除字符串两端的空白字符。
 * @param text text。
 * @return 返回生成的字符串结果。
 */
std::string Trim(std::string_view text) {

    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {

        text.remove_prefix(1);

    }

    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {

        text.remove_suffix(1);

    }

    return std::string(text);

}

/**
 * @brief 按行拆分输入文本。
 * @param text text。
 * @return 返回结果集合。
 */
std::vector<std::string_view> SplitLines(std::string_view text) {

    std::vector<std::string_view> lines;

    while (!text.empty()) {

        const auto pos = text.find('\n');

        if (pos == std::string_view::npos) {

            lines.push_back(text);

            break;

        }

        auto line = text.substr(0, pos);

        if (!line.empty() && line.back() == '\r') {

            line.remove_suffix(1);

        }

        lines.push_back(line);

        text.remove_prefix(pos + 1);

    }

    return lines;

}

/**
 * @brief 将输入值转换为LowerAscii表示。
 * @param value 输入值。
 * @return 返回生成的字符串结果。
 */
std::string ToLowerAscii(std::string value) {

    std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char ch) {

        return static_cast<char>(std::tolower(ch));

    });

    return value;

}

/**
 * @brief 解析负载Type。
 * @param line 待输出的文本内容。
 * @param prefix prefix。
 * @return 返回可用结果；失败时返回空值。
 */
std::optional<int> ParsePayloadType(std::string_view line, std::string_view prefix) {

    if (!StartsWith(line, prefix)) {

        return std::nullopt;

    }

    line.remove_prefix(prefix.size());

    const auto separator = line.find_first_of(" ");

    if (separator == std::string_view::npos) {

        return std::nullopt;

    }

    try {

        return std::stoi(std::string(line.substr(0, separator)));

    } catch (...) {

        return std::nullopt;

    }

}

/**
 * @brief 从远端 SDP 中选择协商后的视频配置。
 * @param sdp sdp。
 * @return 返回对应结果。
 */
NegotiatedVideoConfig SelectNegotiatedVideoConfig(const std::string_view sdp) {

    std::vector<int> ordered_payload_types;

    std::vector<std::pair<int, std::string>> codec_lines;

    std::vector<std::pair<int, std::string>> fmtp_lines;

    for (const auto line_view : SplitLines(sdp)) {

        if (StartsWith(line_view, "m=video ")) {

            const std::string line(line_view);

            std::size_t token_start = 0;

            int token_index = 0;

            while (token_start < line.size()) {

                const auto token_end = line.find(' ', token_start);

                const auto token = line.substr(token_start, token_end == std::string::npos ? std::string::npos

                                                                                           : token_end - token_start);

                if (token_index >= 3) {

                    try {

                        ordered_payload_types.push_back(std::stoi(token));

                    } catch (...) {

                    }

                }

                if (token_end == std::string::npos) {

                    break;

                }

                token_start = token_end + 1;

                ++token_index;

            }

            continue;

        }

        if (const auto payload_type = ParsePayloadType(line_view, "a=rtpmap:"); payload_type.has_value()) {

            const auto separator = line_view.find(' ');

            if (separator != std::string_view::npos) {

                const auto codec = line_view.substr(separator + 1);

                const auto slash = codec.find('/');

                codec_lines.emplace_back(*payload_type,

                                         std::string(slash == std::string_view::npos ? codec : codec.substr(0, slash)));

            }

            continue;

        }

        if (const auto payload_type = ParsePayloadType(line_view, "a=fmtp:"); payload_type.has_value()) {

            const auto separator = line_view.find(' ');

            if (separator != std::string_view::npos) {

                fmtp_lines.emplace_back(*payload_type, Trim(line_view.substr(separator + 1)));

            }

        }

    }

    auto codec_for = [&codec_lines](const int payload_type) -> std::string {

        for (const auto& [candidate_payload, codec] : codec_lines) {

            if (candidate_payload == payload_type) {

                return codec;

            }

        }

        return {};

    };

    auto fmtp_for = [&fmtp_lines](const int payload_type) -> std::string {

        for (const auto& [candidate_payload, fmtp] : fmtp_lines) {

            if (candidate_payload == payload_type) {

                return fmtp;

            }

        }

        return {};

    };

    auto score_fmtp = [](const std::string& fmtp) {

        const auto normalized = ToLowerAscii(fmtp);

        int score = 0;

        if (normalized.find("packetization-mode=1") != std::string::npos) {

            score += 1000;

        }

        if (normalized.find("profile-level-id=42e01f") != std::string::npos) {

            score += 400;

        } else if (normalized.find("profile-level-id=42c01f") != std::string::npos) {

            score += 350;

        } else if (normalized.find("profile-level-id=42001f") != std::string::npos) {

            score += 300;

        } else if (normalized.find("profile-level-id=42e0") != std::string::npos) {

            score += 300;

        } else if (normalized.find("profile-level-id=42c0") != std::string::npos) {

            score += 250;

        } else if (normalized.find("profile-level-id=4200") != std::string::npos) {

            score += 200;

        } else if (normalized.find("profile-level-id=4d00") != std::string::npos) {

            score += 100;

        } else if (normalized.find("profile-level-id=6400") != std::string::npos) {

            score += 50;

        }

        return score;

    };

    NegotiatedVideoConfig best;

    int best_score = -1;

    for (std::size_t index = 0; index < ordered_payload_types.size(); ++index) {

        const auto payload_type = ordered_payload_types[index];

        if (codec_for(payload_type) != "H264") {

            continue;

        }

        const auto fmtp = fmtp_for(payload_type);

        const auto score = score_fmtp(fmtp) - static_cast<int>(index);

        if (score > best_score) {

            best_score = score;

            best.payload_type = payload_type;

            if (!fmtp.empty()) {

                best.profile = fmtp;

            }

        }

    }

    return best;

}

/**
 * @brief 尝试从 SPS 数据中提取 profile-level-id。
 * @param bytes 输入字节缓冲区。
 * @return 返回可用结果；失败时返回空值。
 */
std::optional<std::string> TryExtractSpsProfileLevelId(const std::vector<std::uint8_t>& bytes) {

    const auto find_start_code = [&bytes](const std::size_t from) -> std::size_t {

        for (std::size_t index = from; index + 3 < bytes.size(); ++index) {

            if (bytes[index] != 0 || bytes[index + 1] != 0) {

                continue;

            }

            if (bytes[index + 2] == 1) {

                return index;

            }

            if (index + 4 < bytes.size() && bytes[index + 2] == 0 && bytes[index + 3] == 1) {

                return index;

            }

        }

        return std::string::npos;

    };

    for (auto start = find_start_code(0); start != std::string::npos;) {

        const auto prefix_bytes = bytes[start + 2] == 1 ? 3U : 4U;

        const auto nal_start = start + prefix_bytes;

        auto nal_end = find_start_code(nal_start);

        if (nal_end == std::string::npos) {

            nal_end = bytes.size();

        }

        if (nal_start + 3 < nal_end) {

            const auto nal_type = static_cast<std::uint8_t>(bytes[nal_start] & 0x1F);

            if (nal_type == 7) {

                char buffer[7]{};

                std::snprintf(buffer,

                              sizeof(buffer),

                              "%02x%02x%02x",

                              bytes[nal_start + 1],

                              bytes[nal_start + 2],

                              bytes[nal_start + 3]);

                return std::string(buffer);

            }

        }

        start = nal_end;

    }

    return std::nullopt;

}

/**
 * @brief 解析协商得到的 profile-level-id 字符串。
 * @param fmtp fmtp。
 * @return 返回可用结果；失败时返回空值。
 */
std::optional<std::string> TryParseNegotiatedProfileLevelId(std::string_view fmtp) {
    const auto normalized = ToLowerAscii(std::string(fmtp));
    const auto key = std::string_view("profile-level-id=");
    const auto pos = normalized.find(key);
    if (pos == std::string::npos) {
        return std::nullopt;
    }

    const auto value_pos = pos + key.size();
    if (value_pos + 6 > normalized.size()) {
        return std::nullopt;
    }

    const auto value = normalized.substr(value_pos, 6);
    for (const unsigned char ch : value) {
        if (!std::isxdigit(ch)) {
            return std::nullopt;
        }
    }

    return value;
}

/**
 * @brief 按协商参数重写 SPS 中的 profile-level-id。
 * @param bytes 输入字节缓冲区。
 * @param profile_level_id profilelevelid。
 * @return 返回是否成功或条件是否满足。
 */
bool RewriteSpsProfileLevelId(std::vector<std::uint8_t>& bytes, std::string_view profile_level_id) {
    if (profile_level_id.size() != 6) {
        return false;
    }

    auto parse_byte = [&](const std::size_t offset) -> std::optional<std::uint8_t> {
        try {
            return static_cast<std::uint8_t>(std::stoi(std::string(profile_level_id.substr(offset, 2)), nullptr, 16));
        } catch (...) {
            return std::nullopt;
        }
    };

    const auto profile_idc = parse_byte(0);
    const auto profile_iop = parse_byte(2);
    const auto level_idc = parse_byte(4);
    if (!profile_idc.has_value() || !profile_iop.has_value() || !level_idc.has_value()) {
        return false;
    }

    const auto find_start_code = [&bytes](const std::size_t from) -> std::size_t {
        for (std::size_t index = from; index + 3 < bytes.size(); ++index) {
            if (bytes[index] != 0 || bytes[index + 1] != 0) {
                continue;
            }
            if (bytes[index + 2] == 1) {
                return index;
            }
            if (index + 4 < bytes.size() && bytes[index + 2] == 0 && bytes[index + 3] == 1) {
                return index;
            }
        }
        return std::string::npos;
    };

    for (auto start = find_start_code(0); start != std::string::npos;) {
        const auto prefix_bytes = bytes[start + 2] == 1 ? 3U : 4U;
        const auto nal_start = start + prefix_bytes;
        auto nal_end = find_start_code(nal_start);
        if (nal_end == std::string::npos) {
            nal_end = bytes.size();
        }

        if (nal_start + 3 < nal_end && static_cast<std::uint8_t>(bytes[nal_start] & 0x1F) == 7) {
            bytes[nal_start + 1] = *profile_idc;
            bytes[nal_start + 2] = *profile_iop;
            bytes[nal_start + 3] = *level_idc;
            return true;
        }

        start = nal_end;
    }

    return false;
}

/**
 * @brief 构造对等端Prefix。
 * @param role 角色枚举值。
 * @param session_id 会话标识。
 * @return 返回生成的字符串结果。
 */
std::string MakePeerPrefix(const PeerRole role, const std::string_view session_id) {

    return "[peer:" + std::string(role == PeerRole::Host ? "主机端" : "控制端") + "][" + std::string(session_id) + "] ";

}

/**
 * @brief 构建视频轨道Init。
 * @param role 角色枚举值。
 * @param payload_type payloadtype。
 * @param ssrc ssrc。
 * @param profile profile。
 * @return 返回对应结果。
 */
rtcTrackInit BuildVideoTrackInit(const PeerRole role,

                                 const int payload_type,

                                 const std::uint32_t ssrc,

                                 const char* profile) {

    rtcTrackInit init{};

    init.direction = role == PeerRole::Host ? RTC_DIRECTION_SENDONLY : RTC_DIRECTION_RECVONLY;

    init.codec = RTC_CODEC_H264;

    init.payloadType = payload_type;

    init.ssrc = ssrc;

    init.mid = kVideoMid;

    init.name = kDefaultTrackName;

    init.msid = kDefaultMsid;

    init.trackId = kDefaultTrackId;

    init.profile = profile;

    return init;

}

/**
 * @brief 计算Initial视频SsrcLocal。
 * @param role 角色枚举值。
 * @param session_id 会话标识。
 * @return 返回计算得到的数值结果。
 */
std::uint32_t ComputeInitialVideoSsrcLocal(const PeerRole role, const std::string_view session_id) {

    std::uint32_t hash = 2166136261u;

    for (const unsigned char ch : session_id) {

        hash ^= ch;

        hash *= 16777619u;

    }

    hash ^= role == PeerRole::Host ? 0x484F5354u : 0x4354524Cu;

    hash *= 16777619u;

    return hash == 0 ? 1u : hash;

}

/**
 * @brief 构建视频Packetizer。
 * @param role 角色枚举值。
 * @param session_id 会话标识。
 * @param payload_type payloadtype。
 * @return 返回对应结果。
 */
rtcPacketizerInit BuildVideoPacketizer(const PeerRole role,

                                       const std::string_view session_id,

                                       const int payload_type) {

    rtcPacketizerInit packetizer{};

    packetizer.ssrc = ComputeInitialVideoSsrcLocal(role, session_id);

    packetizer.cname = "rdc-host";

    packetizer.payloadType = static_cast<std::uint8_t>(payload_type);

    packetizer.clockRate = kVideoClockRate;

    packetizer.sequenceNumber = 0;

    packetizer.timestamp = 0;

    packetizer.nalSeparator = RTC_NAL_SEPARATOR_START_SEQUENCE;

    return packetizer;

}

/**
 * @brief 重写 SDP 视频段中的 SSRC 描述。
 * @param sdp sdp。
 * @param ssrc ssrc。
 * @return 返回生成的字符串结果。
 */
std::string RewriteVideoSectionSsrcInSdp(std::string_view sdp, const std::uint32_t ssrc) {

    std::vector<std::string> lines;
    lines.reserve(128);

    std::size_t offset = 0;
    while (offset < sdp.size()) {
        const auto line_end = sdp.find("\r\n", offset);
        if (line_end == std::string_view::npos) {
            lines.emplace_back(sdp.substr(offset));
            offset = sdp.size();
            break;
        }

        lines.emplace_back(sdp.substr(offset, line_end - offset));
        offset = line_end + 2;
    }

    std::vector<std::string> rewritten;
    rewritten.reserve(lines.size() + 8);

    bool in_video_section = false;
    bool injected_video_ssrc = false;

    const auto append_video_ssrc_lines = [&]() {
        if (injected_video_ssrc) {
            return;
        }

        rewritten.push_back("a=msid:" + std::string(kDefaultMsid) + " " + std::string(kDefaultTrackId));
        rewritten.push_back("a=ssrc:" + std::to_string(ssrc) + " cname:rdc-host");
        rewritten.push_back("a=ssrc:" + std::to_string(ssrc) + " msid:" + std::string(kDefaultMsid) + " " + std::string(kDefaultTrackId));
        rewritten.push_back("a=ssrc:" + std::to_string(ssrc) + " mslabel:" + std::string(kDefaultMsid));
        rewritten.push_back("a=ssrc:" + std::to_string(ssrc) + " label:" + std::string(kDefaultTrackId));
        injected_video_ssrc = true;
    };

    for (const auto& line : lines) {
        const bool starts_new_media = line.rfind("m=", 0) == 0;
        if (starts_new_media) {
            if (in_video_section && !injected_video_ssrc) {
                append_video_ssrc_lines();
            }

            in_video_section = line.rfind("m=video ", 0) == 0;
        }

        if (in_video_section && (line.rfind("a=ssrc:", 0) == 0 ||
                                 line.rfind("a=ssrc-group:", 0) == 0 ||
                                 line.rfind("a=msid:", 0) == 0 ||
                                 line.rfind("a=mslabel:", 0) == 0 ||
                                 line.rfind("a=label:", 0) == 0)) {
            continue;
        }

        rewritten.push_back(line);
    }

    if (in_video_section && !injected_video_ssrc) {
        append_video_ssrc_lines();
    }

    std::string result;
    for (const auto& line : rewritten) {
        result += line;
        result += "\r\n";
    }

    return result;
}

/**
 * @brief 执行 对等端会话 相关处理。
 * @param role 角色枚举值。
 * @param session_id 会话标识。
 * @param signal_sender 用于发送信令消息的回调对象。
 * @param message_handler 用于处理消息的回调对象。
 */
}  // namespace

PeerSession::PeerSession(PeerRole role,

                         std::string session_id,

                         SignalSender signal_sender,

                         MessageHandler message_handler,

                         VideoSampleHandler video_sample_handler)

    : role_(role),

      session_id_(std::move(session_id)),

      signal_sender_(std::move(signal_sender)),

      message_handler_(std::move(message_handler)),

      video_sample_handler_(std::move(video_sample_handler)) {

}

PeerSession::~PeerSession() {
    Close();
}

/**
 * @brief 启动相关流程。
 */
void PeerSession::Start() {

    const bool should_start = WithLock([this] {

        if (started_) {

            return false;

        }

        started_ = true;

        return true;

    });

    if (!should_start) {

        return;

    }

    if (role_ == PeerRole::Host) {
        EnsureVideoSendLoop();
    }

    Log("开始创建会话链路");

    EnsurePeerConnection();

}

/**
 * @brief 处理信令。
 * @param payload 协议负载数据。
 */
void PeerSession::HandleSignal(const Json& payload) {

    const auto kind = payload.value("kind", "");

    if (kind == "description") {

        HandleRemoteDescription(payload);

        return;

    }

    if (kind == "candidate") {

        HandleRemoteCandidate(payload);

    }

}

/**
 * @brief 发送控制。
 * @param payload 协议负载数据。
 */
void PeerSession::SendControl(const Json& payload) {

    const auto channel = SelectControlChannelIdForSend();

    if (channel < 0 || !rtcIsOpen(channel)) {

        return;

    }

    const auto data = payload.dump();

    if (rtcSendMessage(channel, data.c_str(), static_cast<int>(data.size())) != RTC_ERR_SUCCESS) {

        Log("发送控制通道数据失败");

    }

}

int PeerSession::SelectControlChannelIdForSend() const {

    return WithLock([this] {

        if (control_channel_id_ >= 0 && rtcIsOpen(control_channel_id_)) {

            return control_channel_id_;

        }

        if (control_realtime_channel_id_ >= 0 && rtcIsOpen(control_realtime_channel_id_)) {

            return control_realtime_channel_id_;

        }

        return -1;

    });

}

void PeerSession::RegisterDataChannel(const int data_channel_id, const std::string_view label) {

    WithLock([this, data_channel_id, label] {

        if (label == kControlChannelLabel) {

            control_channel_id_ = data_channel_id;

            control_channel_label_ = std::string(label);

        } else if (label == kRealtimeControlChannelLabel) {

            control_realtime_channel_id_ = data_channel_id;

            control_realtime_channel_label_ = std::string(label);

        }

    });

}

std::string PeerSession::ResolveDataChannelLabel(const int data_channel_id) const {

    return WithLock([this, data_channel_id] {

        if (data_channel_id == control_channel_id_) {

            return control_channel_label_;

        }

        if (data_channel_id == control_realtime_channel_id_) {

            return control_realtime_channel_label_;

        }

        return GetDataChannelLabel(data_channel_id);

    });

}

/**
 * @brief 发送视频帧。
 * @param frame 视频帧对象。
 */
void PeerSession::SendVideoFrame(const agent::encoder::EncodedVideoFrame& frame) {
    EnqueueVideoFrame(std::make_shared<agent::encoder::EncodedVideoFrame>(frame));
}

void PeerSession::EnqueueVideoFrame(std::shared_ptr<const agent::encoder::EncodedVideoFrame> frame) {
    if (role_ != PeerRole::Host || frame == nullptr || frame->bytes.empty()) {
        return;
    }

    bool dropped_previous_frame = false;
    std::uint64_t dropped_frame_count = 0;

    {
        std::scoped_lock lock(queued_video_frame_mutex_);

        if (!accepting_video_frames_ || video_send_stop_requested_) {
            return;
        }

        if (queued_video_frame_ != nullptr &&
            queued_video_frame_->is_key_frame &&
            !frame->is_key_frame) {
            return;
        }

        dropped_previous_frame = queued_video_frame_ != nullptr;
        if (dropped_previous_frame) {
            dropped_frame_count = ++dropped_queued_video_frames_;
        }

        queued_video_frame_ = std::move(frame);
    }

    queued_video_frame_cv_.notify_one();

    if (dropped_previous_frame &&
        (dropped_frame_count <= 5 || dropped_frame_count % 60 == 0)) {
        Log("视频发送队列已丢弃旧帧以保持低时延, 累计丢弃=" +
            std::to_string(dropped_frame_count));
    }
}

void PeerSession::EnsureVideoSendLoop() {
    {
        std::scoped_lock lock(queued_video_frame_mutex_);

        if (video_send_thread_.joinable()) {
            return;
        }

        accepting_video_frames_ = true;
        video_send_stop_requested_ = false;
        dropped_queued_video_frames_ = 0;
        queued_video_frame_.reset();
    }

    video_send_thread_ = std::thread([this] {
        RunVideoSendLoop();
    });
}

void PeerSession::StopVideoSendLoop() {
    std::thread send_thread;

    {
        std::scoped_lock lock(queued_video_frame_mutex_);

        accepting_video_frames_ = false;
        video_send_stop_requested_ = true;
        queued_video_frame_.reset();

        if (video_send_thread_.joinable()) {
            send_thread = std::move(video_send_thread_);
        } else {
            dropped_queued_video_frames_ = 0;
        }
    }

    queued_video_frame_cv_.notify_all();

    if (send_thread.joinable()) {
        send_thread.join();
    }

    {
        std::scoped_lock lock(queued_video_frame_mutex_);
        dropped_queued_video_frames_ = 0;
        queued_video_frame_.reset();
    }
}

void PeerSession::RunVideoSendLoop() {
    while (true) {
        std::shared_ptr<const agent::encoder::EncodedVideoFrame> frame;

        {
            std::unique_lock lock(queued_video_frame_mutex_);
            queued_video_frame_cv_.wait(lock, [this] {
                return video_send_stop_requested_ || queued_video_frame_ != nullptr;
            });

            if (video_send_stop_requested_) {
                return;
            }

            frame = std::move(queued_video_frame_);
            queued_video_frame_.reset();
        }

        if (frame != nullptr) {
            SendVideoFrameNow(*frame);
        }
    }
}

void PeerSession::SendVideoFrameNow(const agent::encoder::EncodedVideoFrame& frame) {
    if (frame.bytes.empty()) {
        return;
    }

    int video_track_id = -1;
    std::uint32_t current_timestamp = 0;
    std::uint64_t sent_frame_index = 0;
    bool should_log = false;
    std::string track_mid;
    std::string negotiated_video_profile;

    {
        std::scoped_lock lock(mutex_);

        video_track_id = video_track_id_;

        const bool can_send_video = role_ == PeerRole::Host
            ? (video_track_id >= 0 && rtcIsOpen(video_track_id))
            : video_track_open_;

        if (video_track_id < 0 || !can_send_video) {
            return;
        }

        current_timestamp = video_rtp_timestamp_;
        video_rtp_timestamp_ += ComputeRtpTimestampDelta(frame.sample_duration_hns);
        sent_frame_index = ++sent_video_frames_;
        should_log = sent_frame_index <= 5 || sent_frame_index % 60 == 0;

        if (should_log) {
            track_mid = video_track_mid_;
            negotiated_video_profile = negotiated_video_profile_;
        }
    }

    if (rtcSetTrackRtpTimestamp(video_track_id, current_timestamp) != RTC_ERR_SUCCESS) {
        Log("设置视频轨 RTP 时间戳失败");
        return;
    }

    const agent::encoder::EncodedVideoFrame* frame_to_send = &frame;
    agent::encoder::EncodedVideoFrame rewritten_frame;
    std::optional<std::string> original_profile_level_id;
    std::optional<std::string> sent_profile_level_id;
    bool rewrote_keyframe_sps = false;

    if (role_ == PeerRole::Host && frame.is_key_frame) {
        const auto negotiated_profile_level_id = TryParseNegotiatedProfileLevelId(negotiated_video_profile);
        if (negotiated_profile_level_id.has_value()) {
            original_profile_level_id = TryExtractSpsProfileLevelId(frame.bytes);
            if (!original_profile_level_id.has_value() || *original_profile_level_id != *negotiated_profile_level_id) {
                rewritten_frame = frame;
                if (RewriteSpsProfileLevelId(rewritten_frame.bytes, *negotiated_profile_level_id)) {
                    frame_to_send = &rewritten_frame;
                    rewrote_keyframe_sps = true;
                }
            }
        }

        sent_profile_level_id = TryExtractSpsProfileLevelId(frame_to_send->bytes);
    }

    if (rewrote_keyframe_sps) {
        Log("已按协商参数重写关键帧 SPS: " +
            (original_profile_level_id.has_value() ? *original_profile_level_id : std::string("unknown")) +
            " -> " +
            (sent_profile_level_id.has_value() ? *sent_profile_level_id : std::string("unknown")));
    }

    if (rtcSendMessage(video_track_id,
                       reinterpret_cast<const char*>(frame_to_send->bytes.data()),
                       static_cast<int>(frame_to_send->bytes.size())) != RTC_ERR_SUCCESS) {
        Log("发送已编码的 H.264 数据失败");
        return;
    }

    if (should_log) {
        Log("已在 " + track_mid +
            " 视频轨上发送编码桌面帧, 帧序号=" + std::to_string(sent_frame_index) +
            ", 字节数=" + std::to_string(frame_to_send->bytes.size()) +
            ", 关键帧=" + (frame.is_key_frame ? std::string("是") : std::string("否")));
    }

    if (role_ == PeerRole::Host && frame.is_key_frame) {
        bool should_log_profile = false;

        {
            std::scoped_lock lock(mutex_);

            if (!logged_first_keyframe_profile_) {
                logged_first_keyframe_profile_ = true;
                should_log_profile = true;
            }
        }

        if (should_log_profile) {
            if (const auto profile_level_id =
                    sent_profile_level_id.has_value()
                        ? sent_profile_level_id
                        : TryExtractSpsProfileLevelId(frame_to_send->bytes);
                profile_level_id.has_value()) {
                Log("首个关键帧 SPS 信息: profile-level-id=" + *profile_level_id);
            } else {
                Log("首个关键帧未能解析出 SPS profile-level-id");
            }
        }
    }
}

/**
 * @brief 判断视频Ready是否满足条件。
 * @return 返回是否成功或条件是否满足。
 */
bool PeerSession::IsVideoReady() const {

    return WithLock([this] {

        if (video_track_id_ < 0) {

            return false;

        }

        return role_ == PeerRole::Host ? rtcIsOpen(video_track_id_) : video_track_open_;

    });

}

/**
 * @brief 消费PendingKeyframeRequest。
 * @return 返回是否成功或条件是否满足。
 */
bool PeerSession::ConsumePendingKeyframeRequest() {

    return WithLock([this] {

        const bool requested = pending_video_keyframe_request_;

        pending_video_keyframe_request_ = false;

        return requested;

    });

}

/**
 * @brief 关闭相关流程。
 */
void PeerSession::Close() {
    StopVideoSendLoop();

    struct Handles {

        int peer_connection_id = -1;

        int control_channel_id = -1;

        int control_realtime_channel_id = -1;

        int video_track_id = -1;

    };

    const auto handles = WithLock([this] {

        Handles result;

        result.peer_connection_id = peer_connection_id_;

        result.control_channel_id = control_channel_id_;

        result.control_realtime_channel_id = control_realtime_channel_id_;

        result.video_track_id = video_track_id_;

        peer_connection_id_ = -1;

        control_channel_id_ = -1;

        control_realtime_channel_id_ = -1;

        video_track_id_ = -1;

        started_ = false;

        remote_description_set_ = false;

        video_track_open_ = false;

        pending_video_keyframe_request_ = false;

        peer_connected_ = false;

        logged_first_keyframe_profile_ = false;

        sent_video_frames_ = 0;

        received_video_samples_ = 0;

        video_rtp_timestamp_ = 0;

        pending_candidates_.clear();

        return result;

    });

    auto clear_callbacks = [](const int id) {

        if (id < 0) {

            return;

        }

        rtcSetUserPointer(id, nullptr);

        rtcSetOpenCallback(id, nullptr);

        rtcSetClosedCallback(id, nullptr);

        rtcSetErrorCallback(id, nullptr);

        rtcSetMessageCallback(id, nullptr);

    };

    clear_callbacks(handles.control_channel_id);

    clear_callbacks(handles.control_realtime_channel_id);

    clear_callbacks(handles.video_track_id);

    if (handles.peer_connection_id >= 0) {

        rtcSetUserPointer(handles.peer_connection_id, nullptr);

        rtcSetLocalDescriptionCallback(handles.peer_connection_id, nullptr);

        rtcSetLocalCandidateCallback(handles.peer_connection_id, nullptr);

        rtcSetStateChangeCallback(handles.peer_connection_id, nullptr);

        rtcSetDataChannelCallback(handles.peer_connection_id, nullptr);

        rtcSetTrackCallback(handles.peer_connection_id, nullptr);

        rtcClosePeerConnection(handles.peer_connection_id);

        rtcDeletePeerConnection(handles.peer_connection_id);

    }

}

/**
 * @brief 确保对等端连接已就绪。
 */
void PeerSession::EnsurePeerConnection() {

    const auto existing_peer_connection = WithLock([this] {

        return peer_connection_id_;

    });

    if (existing_peer_connection >= 0) {

        return;

    }

    Log("正在创建 PeerConnection");

    rtcConfiguration config{};

    config.disableAutoNegotiation = true;

    config.enableIceTcp = false;

    config.mtu = RTC_DEFAULT_MTU;

    config.maxMessageSize = 262144;

    const int pc = rtcCreatePeerConnection(&config);

    if (pc < 0) {

        throw std::runtime_error("创建 PeerConnection 失败");

    }

    rtcSetUserPointer(pc, this);

    rtcSetLocalDescriptionCallback(pc, &PeerSession::HandleLocalDescription);

    rtcSetLocalCandidateCallback(pc, &PeerSession::HandleLocalCandidate);

    rtcSetStateChangeCallback(pc, &PeerSession::HandleStateChange);

    rtcSetDataChannelCallback(pc, &PeerSession::HandleDataChannel);

    rtcSetTrackCallback(pc, &PeerSession::HandleTrack);

    WithLock([this, pc] {

        peer_connection_id_ = pc;

    });

    if (role_ == PeerRole::Controller) {

        EnsureVideoTrack();

        rtcDataChannelInit init{};

        const int dc = rtcCreateDataChannelEx(pc, kControlChannelLabel, &init);

        if (dc < 0) {

            throw std::runtime_error("创建控制数据通道失败");

        }

        RegisterDataChannel(dc, kControlChannelLabel);

        ConfigureDataChannel(dc);

        Log("已创建控制数据通道");

        if (rtcSetLocalDescription(pc, "offer") != RTC_ERR_SUCCESS) {

            throw std::runtime_error("生成本地 Offer 失败");

        }

        Log("已请求生成本地描述");

    }

}

/**
 * @brief 确保视频轨道已就绪。
 */
void PeerSession::EnsureVideoTrack() {

    const auto state = WithLock([this] {

        return std::make_tuple(peer_connection_id_, video_track_id_, negotiated_video_payload_type_, negotiated_video_profile_);

    });

    const auto pc = std::get<0>(state);

    const auto existing_track = std::get<1>(state);

    const auto payload_type = std::get<2>(state);

    const auto profile = std::get<3>(state);

    if (existing_track >= 0) {

        return;

    }

    if (pc < 0) {

        throw std::runtime_error("创建视频轨前 PeerConnection 尚未就绪");

    }

    auto track_init = BuildVideoTrackInit(role_,

                                          payload_type,

                                          ComputeInitialVideoSsrc(role_, session_id_),

                                          profile.c_str());

    const int track_id = rtcAddTrackEx(pc, &track_init);

    if (track_id < 0) {

        throw std::runtime_error("创建 H.264 视频轨失败");

    }

    WithLock([this, track_id] {

        video_track_id_ = track_id;

        video_track_mid_ = kVideoMid;

        video_track_open_ = rtcIsOpen(track_id);

    });

    ConfigureTrack(track_id);

    if (role_ == PeerRole::Host) {

        auto packetizer = BuildVideoPacketizer(role_, session_id_, payload_type);

        if (rtcSetH264Packetizer(track_id, &packetizer) != RTC_ERR_SUCCESS ||

            rtcChainRtcpSrReporter(track_id) != RTC_ERR_SUCCESS ||

            rtcChainPliHandler(track_id, &PeerSession::HandleVideoPli) != RTC_ERR_SUCCESS) {

            throw std::runtime_error("配置 H.264 视频轨失败");

        }

    } else {

        if (rtcChainRtcpReceivingSession(track_id) != RTC_ERR_SUCCESS) {

            throw std::runtime_error("配置控制端视频接收轨失败");

        }

    }

    Log("已配置 H.264 视频轨，mid=video, payloadType=" + std::to_string(payload_type) +

        ", profile=" + profile);

}

/**
 * @brief 处理Remote描述。
 * @param payload 协议负载数据。
 */
void PeerSession::HandleRemoteDescription(const Json& payload) {

    const auto sdp = payload.value("sdp", "");

    const auto sdp_type = payload.value("sdpType", "");

    if (sdp.empty() || sdp_type.empty()) {

        return;

    }

    EnsurePeerConnection();

    int pc = -1;

    bool should_create_answer = false;

    if (role_ == PeerRole::Host && sdp_type == "offer") {

        const auto negotiated = SelectNegotiatedVideoConfig(sdp);

        WithLock([this, &negotiated, &pc] {

            negotiated_video_payload_type_ = negotiated.payload_type;

            negotiated_video_profile_ = negotiated.profile;

            pc = peer_connection_id_;

        });

        Log("已根据远端 Offer 选择 H.264 协商参数，payloadType=" + std::to_string(negotiated.payload_type) +

            ", profile=" + negotiated.profile);

        EnsureVideoTrack();

        should_create_answer = true;

    } else {

        pc = WithLock([this] {

            return peer_connection_id_;

        });

    }

    if (pc < 0) {

        throw std::runtime_error("设置远端描述前 PeerConnection 尚未就绪");

    }

    if (rtcSetRemoteDescription(pc, sdp.c_str(), sdp_type.c_str()) != RTC_ERR_SUCCESS) {

        throw std::runtime_error("设置远端 SDP 失败");

    }

    WithLock([this] {

        remote_description_set_ = true;

    });

    FlushPendingCandidates();

    if (should_create_answer) {

        if (rtcSetLocalDescription(pc, "answer") != RTC_ERR_SUCCESS) {

            throw std::runtime_error("生成本地 Answer 失败");

        }

        Log("已请求生成本地描述");

    }

}

/**
 * @brief 处理Remote候选项。
 * @param payload 协议负载数据。
 */
void PeerSession::HandleRemoteCandidate(const Json& payload) {

    const auto candidate = payload.value("candidate", "");

    const auto mid = payload.value("mid", "");

    if (candidate.empty()) {

        return;

    }

    const bool remote_description_ready = WithLock([this] {

        return remote_description_set_;

    });

    if (!remote_description_ready) {

        QueuePendingCandidate(payload);

        return;

    }

    const auto pc = WithLock([this] {

        return peer_connection_id_;

    });

    if (pc < 0) {

        return;

    }

    if (rtcAddRemoteCandidate(pc, candidate.c_str(), mid.empty() ? nullptr : mid.c_str()) != RTC_ERR_SUCCESS) {

        Log("添加远端 ICE 候选失败");

    }

}

/**
 * @brief 刷新PendingCandidates。
 */
void PeerSession::FlushPendingCandidates() {

    std::vector<Json> pending;

    {

        std::scoped_lock lock(mutex_);

        pending.swap(pending_candidates_);

    }

    for (const auto& candidate : pending) {

        HandleRemoteCandidate(candidate);

    }

}

/**
 * @brief 将Pending候选项加入队列。
 * @param payload 协议负载数据。
 */
void PeerSession::QueuePendingCandidate(const Json& payload) {

    WithLock([this, &payload] {

        pending_candidates_.push_back(payload);

    });

}

/**
 * @brief 发送信令。
 * @param payload 协议负载数据。
 */
void PeerSession::SendSignal(const Json& payload) const {

    if (signal_sender_ != nullptr) {

        signal_sender_(payload);

    }

}

/**
 * @brief 记录相关流程。
 * @param message 待处理的消息对象。
 */
void PeerSession::Log(const std::string& message) const {

    protocol::common::WriteInfoLine(MakePeerPrefix(role_, session_id_) + message);

}

/**
 * @brief 配置数据Channel。
 * @param data_channel_id 数据通道标识。
 */
void PeerSession::ConfigureDataChannel(int data_channel_id) {

    rtcSetUserPointer(data_channel_id, this);

    rtcSetOpenCallback(data_channel_id, &PeerSession::HandleChannelOpen);

    rtcSetClosedCallback(data_channel_id, &PeerSession::HandleChannelClosed);

    rtcSetErrorCallback(data_channel_id, &PeerSession::HandleChannelError);

    rtcSetMessageCallback(data_channel_id, &PeerSession::HandleChannelMessage);

}

/**
 * @brief 配置轨道。
 * @param track_id 媒体轨标识。
 */
void PeerSession::ConfigureTrack(int track_id) {

    rtcSetUserPointer(track_id, this);

    rtcSetOpenCallback(track_id, &PeerSession::HandleTrackOpen);

    rtcSetClosedCallback(track_id, &PeerSession::HandleTrackClosed);

    rtcSetErrorCallback(track_id, &PeerSession::HandleTrackError);

    rtcSetMessageCallback(track_id, &PeerSession::HandleTrackMessageCallback);

}

/**
 * @brief 处理数据Channel消息。
 * @param channel_label 数据通道标签。
 * @param payload 协议负载数据。
 * @param size 字节长度。
 */
void PeerSession::HandleDataChannelMessage(std::string_view channel_label, const char* payload, const std::size_t size) {

    const auto parsed = Json::parse(payload, payload + size, nullptr, false);

    if (parsed.is_discarded()) {

        Log("控制数据通道收到无法解析的 JSON 数据");

        return;

    }

    if (message_handler_ != nullptr) {

        message_handler_(channel_label, parsed);

    }

}

/**
 * @brief 处理轨道消息。
 * @param track_id 媒体轨标识。
 * @param message 待处理的消息对象。
 * @param size 字节长度。
 */
void PeerSession::HandleTrackMessage(int track_id, const char* message, const std::size_t size) {

    if (message == nullptr || size == 0) {

        return;

    }

    std::string track_mid;

    std::uint64_t sequence = 0;

    bool should_log = false;

    VideoSampleHandler video_handler;

    PeerRole role = role_;

    {

        std::scoped_lock lock(mutex_);

        if (track_id != video_track_id_) {

            return;

        }

        track_mid = video_track_mid_;

        sequence = ++received_video_samples_;

        should_log = sequence <= 5 || sequence % 60 == 0;

        if (role_ == PeerRole::Controller) {

            video_handler = video_sample_handler_;

        }

    }

    if (role == PeerRole::Controller) {

        if (video_handler != nullptr) {

            video_handler(reinterpret_cast<const std::uint8_t*>(message), size);

        }

        if (should_log) {

            Log("在 " + track_mid + " 视频轨上收到媒体负载, 样本序号=" + std::to_string(sequence) +

                ", 字节数=" + std::to_string(size));

        }

    } else if (should_log) {

        Log("在 " + track_mid + " 视频轨上收到反馈包, 包序号=" + std::to_string(sequence) +

            ", 字节数=" + std::to_string(size));

    }

}

/**
 * @brief 计算Initial视频Ssrc。
 * @param role 角色枚举值。
 * @param session_id 会话标识。
 * @return 返回计算得到的数值结果。
 */
std::uint32_t PeerSession::ComputeInitialVideoSsrc(const PeerRole role, const std::string_view session_id) {

    std::uint32_t hash = 2166136261u;

    for (const unsigned char ch : session_id) {

        hash ^= ch;

        hash *= 16777619u;

    }

    hash ^= role == PeerRole::Host ? 0x484F5354u : 0x4354524Cu;

    hash *= 16777619u;

    return hash == 0 ? 1u : hash;

}

/**
 * @brief 计算RTP时间戳Delta。
 * @param sample_duration_hns 样本时长，单位为 100ns。
 * @return 返回计算得到的数值结果。
 */
std::uint32_t PeerSession::ComputeRtpTimestampDelta(const std::int64_t sample_duration_hns) {

    if (sample_duration_hns <= 0) {

        return kDefaultRtpTimestampDelta;

    }

    const auto scaled = static_cast<std::uint64_t>(sample_duration_hns) * kVideoClockRate;

    const auto delta = scaled / 10'000'000ULL;

    return static_cast<std::uint32_t>(delta == 0 ? 1 : delta);

}

/**
 * @brief 将连接状态转换为可读字符串。
 * @param state 状态枚举值。
 * @return 返回生成的字符串结果。
 */
std::string PeerSession::StateToString(const rtcState state) {

    switch (state) {
    case RTC_NEW:
        return "新建";
    case RTC_CONNECTING:
        return "连接中";
    case RTC_CONNECTED:
        return "已连接";
    case RTC_DISCONNECTED:
        return "已断开";
    case RTC_FAILED:
        return "失败";
    case RTC_CLOSED:
        return "已关闭";
    }

    return "未知";

}

/**
 * @brief 处理Local描述。
 * @param int int。
 * @param sdp sdp。
 * @param type 消息类型。
 * @param ptr ptr。
 */
void RTC_API PeerSession::HandleLocalDescription(int /*pc*/, const char* sdp, const char* type, void* ptr) {

    auto* self = static_cast<PeerSession*>(ptr);

    if (self == nullptr || sdp == nullptr || type == nullptr) {
        return;
    }

    std::string local_sdp = sdp;

    if (self->role_ == PeerRole::Host && std::string_view(type) == "answer") {
        const auto video_ssrc = ComputeInitialVideoSsrcLocal(self->role_, self->session_id_);
        local_sdp = RewriteVideoSectionSsrcInSdp(local_sdp, video_ssrc);
        self->Log("已在本地 SDP 中写入视频 SSRC: " + std::to_string(video_ssrc));
    }

    self->SendSignal(Json{
        {"kind", "description"},
        {"sdp", local_sdp},
        {"sdpType", type}
    });

}

/**
 * @brief 处理Local候选项。
 * @param int int。
 * @param cand cand。
 * @param mid mid。
 * @param ptr ptr。
 */
void RTC_API PeerSession::HandleLocalCandidate(int /*pc*/, const char* cand, const char* mid, void* ptr) {

    auto* self = static_cast<PeerSession*>(ptr);

    if (self == nullptr || cand == nullptr || *cand == '\0') {

        return;

    }

    Json payload{

        {"kind", "candidate"},

        {"candidate", cand}

    };

    if (mid != nullptr && *mid != '\0') {

        payload["mid"] = mid;

    }

    self->SendSignal(payload);

}

/**
 * @brief 处理状态Change。
 * @param int int。
 * @param state 状态枚举值。
 * @param ptr ptr。
 */
void RTC_API PeerSession::HandleStateChange(int /*pc*/, rtcState state, void* ptr) {

    auto* self = static_cast<PeerSession*>(ptr);

    if (self == nullptr) {

        return;

    }

    {

        std::scoped_lock lock(self->mutex_);

        self->peer_connected_ = state == RTC_CONNECTED;

        if (state == RTC_CLOSED || state == RTC_FAILED) {

            self->video_track_open_ = false;

        }

    }

    self->Log("PeerConnection 状态 -> " + StateToString(state));

}

/**
 * @brief 处理数据Channel。
 * @param int int。
 * @param dc dc。
 * @param ptr ptr。
 */
void RTC_API PeerSession::HandleDataChannel(int /*pc*/, int dc, void* ptr) {

    auto* self = static_cast<PeerSession*>(ptr);

    if (self == nullptr) {

        return;

    }

    const auto label = GetDataChannelLabel(dc);

    self->RegisterDataChannel(dc, label);

    self->ConfigureDataChannel(dc);

    self->Log("已接受传入数据通道: " + label);

}

/**
 * @brief 处理轨道。
 * @param int int。
 * @param tr tr。
 * @param ptr ptr。
 */
void RTC_API PeerSession::HandleTrack(int /*pc*/, int tr, void* ptr) {

    auto* self = static_cast<PeerSession*>(ptr);

    if (self == nullptr) {

        return;

    }

    std::string track_mid;

    bool adopted_sender_track = false;

    int negotiated_payload_type = kDefaultH264PayloadType;

    {

        std::scoped_lock lock(self->mutex_);

        if (self->role_ == PeerRole::Host && self->video_track_id_ >= 0 && self->video_track_id_ != tr) {

            self->video_track_id_ = tr;

            self->video_track_open_ = rtcIsOpen(tr);

            self->pending_video_keyframe_request_ = true;

            adopted_sender_track = true;

            negotiated_payload_type = self->negotiated_video_payload_type_;

        } else if (self->video_track_id_ < 0) {

            self->video_track_id_ = tr;

            self->video_track_open_ = rtcIsOpen(tr);

            self->pending_video_keyframe_request_ = self->role_ == PeerRole::Host;

        }

        if (self->video_track_mid_.empty()) {

            self->video_track_mid_ = kVideoMid;

        }

        track_mid = self->video_track_mid_;

    }

    if (adopted_sender_track) {

        self->ConfigureTrack(tr);

        auto packetizer = BuildVideoPacketizer(self->role_, self->session_id_, negotiated_payload_type);

        if (rtcSetH264Packetizer(tr, &packetizer) == RTC_ERR_SUCCESS &&

            rtcChainRtcpSrReporter(tr) == RTC_ERR_SUCCESS &&

            rtcChainPliHandler(tr, &PeerSession::HandleVideoPli) == RTC_ERR_SUCCESS) {

            self->Log("已切换到协商后的视频发送轨，mid=" + track_mid + ", handle=" + std::to_string(tr));

        } else {

            self->Log("切换协商后的视频发送轨失败，handle=" + std::to_string(tr));

        }

    }

    self->Log("已接受传入轨道，mid=" + track_mid + ", handle=" + std::to_string(tr));

}

/**
 * @brief 处理Channel打开。
 * @param id id。
 * @param ptr ptr。
 */
void RTC_API PeerSession::HandleChannelOpen(int id, void* ptr) {

    auto* self = static_cast<PeerSession*>(ptr);

    if (self == nullptr) {

        return;

    }

    const auto label = self->ResolveDataChannelLabel(id);

    self->Log("数据通道已打开 -> " + label);

    if (self->role_ == PeerRole::Controller && label == kControlChannelLabel) {

        self->SendControl(Json{

            {"type", "ping"},

            {"seq", 1},

            {"ts", static_cast<std::int64_t>(std::time(nullptr))}

        });

    }

}

/**
 * @brief 处理ChannelClosed。
 * @param id id。
 * @param ptr ptr。
 */
void RTC_API PeerSession::HandleChannelClosed(int id, void* ptr) {

    auto* self = static_cast<PeerSession*>(ptr);

    if (self == nullptr) {

        return;

    }

    const auto label = self->ResolveDataChannelLabel(id);

    self->Log("数据通道已关闭 -> " + label);

}

/**
 * @brief 处理Channel错误。
 * @param id id。
 * @param error error。
 * @param ptr ptr。
 */
void RTC_API PeerSession::HandleChannelError(int id, const char* error, void* ptr) {

    auto* self = static_cast<PeerSession*>(ptr);

    if (self == nullptr) {

        return;

    }

    const auto label = self->ResolveDataChannelLabel(id);

    self->Log("数据通道错误，通道=" + label + ": " + (error != nullptr ? std::string(error) : std::string("未知")));

}

/**
 * @brief 处理Channel消息。
 * @param id id。
 * @param message 待处理的消息对象。
 * @param size 字节长度。
 * @param ptr ptr。
 */
void RTC_API PeerSession::HandleChannelMessage(int id, const char* message, int size, void* ptr) {

    auto* self = static_cast<PeerSession*>(ptr);

    if (self == nullptr || message == nullptr || size <= 0) {

        return;

    }

    const auto label = self->ResolveDataChannelLabel(id);

    self->HandleDataChannelMessage(label, message, static_cast<std::size_t>(size));

}

/**
 * @brief 处理轨道打开。
 * @param id id。
 * @param ptr ptr。
 */
void RTC_API PeerSession::HandleTrackOpen(int id, void* ptr) {

    auto* self = static_cast<PeerSession*>(ptr);

    if (self == nullptr) {

        return;

    }

    std::string track_mid;

    bool requested_keyframe = false;

    {

        std::scoped_lock lock(self->mutex_);

        if (id == self->video_track_id_) {

            self->video_track_open_ = true;

            if (self->role_ == PeerRole::Host) {

                self->pending_video_keyframe_request_ = true;

                requested_keyframe = true;

            }

        }

        if (self->video_track_mid_.empty()) {

            self->video_track_mid_ = kVideoMid;

        }

        track_mid = self->video_track_mid_;

    }

    self->Log("媒体轨已打开 -> " + track_mid);

    if (requested_keyframe) {

        self->Log("媒体轨已打开，已安排下一帧输出关键帧");

    }

}

/**
 * @brief 处理轨道Closed。
 * @param id id。
 * @param ptr ptr。
 */
void RTC_API PeerSession::HandleTrackClosed(int id, void* ptr) {

    auto* self = static_cast<PeerSession*>(ptr);

    if (self == nullptr) {

        return;

    }

    std::string track_mid;

    {

        std::scoped_lock lock(self->mutex_);

        if (id == self->video_track_id_) {

            self->video_track_open_ = false;

        }

        track_mid = self->video_track_mid_.empty() ? std::string(kVideoMid) : self->video_track_mid_;

    }

    self->Log("媒体轨已关闭 -> " + track_mid);

}

/**
 * @brief 处理轨道错误。
 * @param id id。
 * @param error error。
 * @param ptr ptr。
 */
void RTC_API PeerSession::HandleTrackError(int id, const char* error, void* ptr) {

    auto* self = static_cast<PeerSession*>(ptr);

    if (self == nullptr) {

        return;

    }

    const auto track_mid = self->WithLock([self] {

        return self->video_track_mid_.empty() ? std::string(kVideoMid) : self->video_track_mid_;

    });

    self->Log("媒体轨错误，轨道=" + track_mid + ": " + (error != nullptr ? std::string(error) : std::string("未知")));

}

/**
 * @brief 处理轨道消息Callback。
 * @param id id。
 * @param message 待处理的消息对象。
 * @param size 字节长度。
 * @param ptr ptr。
 */
void RTC_API PeerSession::HandleTrackMessageCallback(int id, const char* message, int size, void* ptr) {

    auto* self = static_cast<PeerSession*>(ptr);

    if (self == nullptr || message == nullptr || size <= 0) {

        return;

    }

    self->HandleTrackMessage(id, message, static_cast<std::size_t>(size));

}

/**
 * @brief 处理视频Pli。
 * @param int int。
 * @param ptr ptr。
 */
void RTC_API PeerSession::HandleVideoPli(int /*tr*/, void* ptr) {

    auto* self = static_cast<PeerSession*>(ptr);

    if (self == nullptr) {

        return;

    }

    std::string track_mid;

    {

        std::scoped_lock lock(self->mutex_);

        self->pending_video_keyframe_request_ = true;

        track_mid = self->video_track_mid_.empty() ? std::string(kVideoMid) : self->video_track_mid_;

    }

    self->Log("在 " + track_mid + " 上收到 PLI，已安排下一帧输出关键帧");

}

/**
 * @brief 获取数据ChannelLabel。
 * @param data_channel_id 数据通道标识。
 * @return 返回生成的字符串结果。
 */
std::string PeerSession::GetDataChannelLabel(const int data_channel_id) {

    std::array<char, 256> buffer{};

    const auto result = rtcGetDataChannelLabel(data_channel_id, buffer.data(), static_cast<int>(buffer.size()));

    if (result >= 0) {

        return std::string(buffer.data());

    }

    return kControlChannelLabel;

}

/**
 * @brief 获取轨道Mid。
 * @param track_id 媒体轨标识。
 * @return 返回生成的字符串结果。
 */
std::string PeerSession::GetTrackMid(const int track_id) {

    std::array<char, 256> buffer{};

    const auto result = rtcGetTrackMid(track_id, buffer.data(), static_cast<int>(buffer.size()));

    if (result >= 0) {

        return std::string(buffer.data());

    }

    return kVideoMid;

}

}  // namespace rdc
