/**
 * @file host_client.cpp
 * @brief 实现 agent/session/host_client 相关的类型、函数与流程。
 */

#include "host_client.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include "desktop_streamer.hpp"
#include "../encoder/bgra_to_nv12_converter.hpp"
#include "../platform/windows/d3d11_desktop_frame_reader.hpp"
#include "../platform/windows/dxgi_desktop_duplicator.hpp"
#include "../platform/windows/x264_h264_encoder.hpp"
#include "../../protocol/common/buffer_utils.hpp"
#include "../../protocol/common/console_logger.hpp"

namespace rdc {

namespace {

/**
 * @brief 读取环境变量无符号整数。
 * @param name 名称字符串。
 * @param fallback 回退值。
 * @return 返回对应结果。
 */
template <typename Unsigned>
Unsigned ReadEnvUnsigned(const char* name, const Unsigned fallback) {
    static_assert(std::is_unsigned_v<Unsigned>, "ReadEnvUnsigned 仅支持无符号整数类型");

    if (const char* value = std::getenv(name); value != nullptr) {
        try {
            return static_cast<Unsigned>(std::stoull(value));
        } catch (...) {
            return fallback;
        }
    }

    return fallback;
}

/**
 * @brief 判断采集SmokeEnabled是否满足条件。
 * @return 返回是否成功或条件是否满足。
 */
bool IsCaptureSmokeEnabled() {
    return std::getenv("RDC_CAPTURE_SMOKE") != nullptr || std::getenv("RDC_ENCODE_SMOKE") != nullptr;
}

/**
 * @brief 判断采集SmokeOnlyEnabled是否满足条件。
 * @return 返回是否成功或条件是否满足。
 */
bool IsCaptureSmokeOnlyEnabled() {
    return std::getenv("RDC_CAPTURE_SMOKE_ONLY") != nullptr || std::getenv("RDC_ENCODE_SMOKE_ONLY") != nullptr;
}

/**
 * @brief 判断编码SmokeEnabled是否满足条件。
 * @return 返回是否成功或条件是否满足。
 */
bool IsEncodeSmokeEnabled() {
    return std::getenv("RDC_ENCODE_SMOKE") != nullptr;
}

/**
 * @brief 执行 Host客户端 相关处理。
 * @param signal_url 信令服务地址。
 */
}  // namespace

HostClient::HostClient(std::string signal_url, std::string device_id)
    : signal_url_(std::move(signal_url)),
      device_id_(std::move(device_id)) {
}

/**
 * @brief 运行相关流程。
 * @return 返回状态码或退出码。
 */
int HostClient::Run() {
    if (RunDesktopCaptureSmokeIfEnabled()) {
        return 0;
    }

    desktop_streamer_ = std::make_unique<agent::session::DesktopStreamer>([this]() {
        return CollectActiveVideoSessions();
    });
    desktop_streamer_->Start();

    ConfigureSocket();

    protocol::common::WriteInfoLine("主机端正在连接 " + signal_url_ + "，设备 ID: " + device_id_);
    signal_client_.Connect(signal_url_);

    SendJson(Json{
        {"type", protocol::kRegisterDevice},
        {"deviceId", device_id_},
        {"capabilities", Json{
                             {"controlReliableChannel", true},
                             {"controlRealtimeChannel", true}
                         }}
    });

    std::unique_lock lock(mutex_);
    cv_.wait(lock, [this] {
        return stop_requested_;
    });
    lock.unlock();

    if (desktop_streamer_ != nullptr) {
        desktop_streamer_->Stop();
        desktop_streamer_.reset();
    }

    protocol::common::ForEachValue(sessions_, [](auto& entry) {
        auto& runtime = entry.second;
        if (runtime.peer_session != nullptr) {
            runtime.peer_session->Close();
        }
    });
    sessions_.clear();

    signal_client_.Close();

    return 0;
}

/**
 * @brief 配置套接字。
 */
void HostClient::ConfigureSocket() {
    signal_client_.SetClosedHandler([this]() {
        protocol::common::WriteInfoLine("主机端信令连接已关闭");
        Stop();
    });

    signal_client_.SetErrorHandler([](const std::string& error) {
        protocol::common::WriteErrorLine("主机端信令错误: " + error);
    });

    signal_client_.SetMessageHandler([this](const Json& message) {
        HandleMessage(message);
    });
}

/**
 * @brief 运行桌面采集SmokeIfEnabled。
 * @return 返回是否成功或条件是否满足。
 */
bool HostClient::RunDesktopCaptureSmokeIfEnabled() const {
    if (!IsCaptureSmokeEnabled()) {
        return false;
    }

    const auto output_index = ReadEnvUnsigned<std::uint32_t>("RDC_CAPTURE_OUTPUT", 0);
    const auto frame_limit = ReadEnvUnsigned<std::uint32_t>("RDC_CAPTURE_SMOKE_FRAMES", 1);
    const auto timeout_ms = ReadEnvUnsigned<std::uint32_t>("RDC_CAPTURE_TIMEOUT_MS", 1000);
    const std::uint32_t configured_attempts =
        ReadEnvUnsigned<std::uint32_t>("RDC_CAPTURE_SMOKE_ATTEMPTS", frame_limit * 5);
    const std::uint32_t max_attempts = configured_attempts < frame_limit ? frame_limit : configured_attempts;

    protocol::common::WriteInfoLine("主机端采集冒烟测试开始: 输出=" + std::to_string(output_index) +
                                    ", 帧数=" + std::to_string(frame_limit) +
                                    ", 超时毫秒=" + std::to_string(timeout_ms) +
                                    ", 最大尝试次数=" + std::to_string(max_attempts));

    agent::platform::windows::DxgiDesktopDuplicator capturer(output_index);
    agent::platform::windows::D3D11DesktopFrameReader frame_reader;
    agent::encoder::BgraToNv12Converter nv12_converter;
    std::unique_ptr<agent::platform::windows::X264H264Encoder> encoder;

    std::uint32_t captured_frames = 0;
    std::uint32_t attempts = 0;
    while (captured_frames < frame_limit && attempts < max_attempts) {
        ++attempts;
        auto frame = capturer.CaptureNextFrame(std::chrono::milliseconds(timeout_ms));
        if (!frame.has_value()) {
            protocol::common::WriteInfoLine("主机端采集冒烟测试第 " + std::to_string(attempts) + " 次尝试超时");
            continue;
        }

        auto raw_frame = frame_reader.Read(*frame);
        std::size_t encoded_frame_count = 0;
        std::size_t total_encoded_bytes = 0;
        std::size_t key_frame_count = 0;
        if (IsEncodeSmokeEnabled()) {
            auto nv12_frame = nv12_converter.Convert(raw_frame);
            if (encoder == nullptr) {
                encoder = std::make_unique<agent::platform::windows::X264H264Encoder>(
                    agent::platform::windows::H264EncoderConfig{
                        .width = nv12_frame.width,
                        .height = nv12_frame.height,
                        .fps_num = 30,
                        .fps_den = 1,
                        .bitrate = 4'000'000,
                        .gop_size = 60,
                    });
            }

            auto collect_stats = [&](const auto& encoded_frame) {
                ++encoded_frame_count;
                total_encoded_bytes += encoded_frame.bytes.size();
                if (encoded_frame.is_key_frame) {
                    ++key_frame_count;
                }
            };

            encoder->EncodeEach(nv12_frame, collect_stats);
            if (encoded_frame_count == 0) {
                encoder->DrainEach(collect_stats);
            }
        }

        ++captured_frames;
        std::string smoke_line = "主机端采集冒烟测试帧 " + std::to_string(captured_frames) + "/" +
                                 std::to_string(frame_limit) +
                                 ": 分辨率=" + std::to_string(frame->width) + "x" + std::to_string(frame->height) +
                                 ", 格式=" + std::string(agent::capture::ToString(frame->pixel_format)) +
                                 ", 回读格式=" + std::string(agent::encoder::ToString(raw_frame.pixel_format)) +
                                 ", 步长=" + std::to_string(raw_frame.stride_bytes) +
                                 ", 字节数=" + std::to_string(raw_frame.bytes.size()) +
                                 ", 脏矩形=" + std::to_string(frame->dirty_rects.size()) +
                                 ", 位移矩形=" + std::to_string(frame->move_rects.size()) +
                                 ", 累积帧数=" + std::to_string(frame->accumulated_frames) +
                                 ", 显示时间戳=" + std::to_string(frame->present_qpc_ticks);

        if (IsEncodeSmokeEnabled()) {
            smoke_line += ", 编码帧数=" + std::to_string(encoded_frame_count) +
                          ", 编码字节数=" + std::to_string(total_encoded_bytes) +
                          ", 关键帧数=" + std::to_string(key_frame_count);
        }

        protocol::common::WriteInfoLine(smoke_line);
    }

    if (captured_frames == 0) {
        throw std::runtime_error("桌面采集冒烟测试未产生任何帧");
    }

    if (IsCaptureSmokeOnlyEnabled()) {
        protocol::common::WriteInfoLine("主机端采集冒烟测试已完成，仅执行采集验证");
        return true;
    }

    return false;
}

/**
 * @brief 收集Active视频Sessions。
 * @return 返回结果集合。
 */
std::vector<std::shared_ptr<PeerSession>> HostClient::CollectActiveVideoSessions() {
    std::vector<std::shared_ptr<PeerSession>> active_sessions;

    std::scoped_lock lock(mutex_);
    active_sessions.reserve(sessions_.size());
    for (const auto& [session_id, runtime] : sessions_) {
        (void)session_id;
        if (runtime.peer_session != nullptr) {
            active_sessions.push_back(runtime.peer_session);
        }
    }

    return active_sessions;
}

/**
 * @brief 处理消息。
 * @param message 待处理的消息对象。
 */
void HostClient::HandleMessage(const Json& message) {
    const std::string type = message.value("type", "");

    if (type == "hello" || type == "registered" || type == "heartbeat_ack") {
        protocol::common::WriteInfoLine("主机端收到信令 <- " + message.dump());
        return;
    }

    if (type == "session_request") {
        const std::string session_id = message.value("sessionId", "");
        protocol::common::WriteInfoLine("主机端接受会话 " + session_id);

        auto session = std::make_shared<PeerSession>(
            PeerRole::Host,
            session_id,
            [this, session_id](const Json& payload) {
                SendJson(Json{
                    {"type", protocol::kSignal},
                    {"sessionId", session_id},
                    {"payload", payload}
                });
            },
            [this, session_id](std::string_view channel_label, const Json& payload) {
                protocol::common::WriteInfoLine("主机端数据 <- [" + std::string(channel_label) + "] " + payload.dump());

                if (channel_label == "control" && payload.value("type", "") == "ping") {
                    std::shared_ptr<PeerSession> current_session;

                    {
                        std::scoped_lock lock(mutex_);
                        if (const auto it = sessions_.find(session_id); it != sessions_.end()) {
                            current_session = it->second.peer_session;
                        }
                    }

                    if (current_session != nullptr) {
                        current_session->SendControl(Json{
                            {"type", "pong"},
                            {"echoSeq", payload.value("seq", 0)}
                        });
                    }
                }
            });

        {
            std::scoped_lock lock(mutex_);
            sessions_[session_id] = SessionRuntime{
                .peer_session = session,
            };
        }

        session->Start();
        SendJson(Json{
            {"type", protocol::kAcceptSession},
            {"sessionId", session_id}
        });
        return;
    }

    if (type == protocol::kSignal) {
        const std::string session_id = message.value("sessionId", "");
        std::shared_ptr<PeerSession> session;

        {
            std::scoped_lock lock(mutex_);
            if (const auto it = sessions_.find(session_id); it != sessions_.end()) {
                session = it->second.peer_session;
            }
        }

        if (session != nullptr) {
            session->HandleSignal(message.value("payload", Json::object()));
        }

        return;
    }

    if (type == "session_closed" || type == "session_failed" || type == "session_rejected") {
        const std::string session_id = message.value("sessionId", "");
        SessionRuntime runtime;

        {
            std::scoped_lock lock(mutex_);
            if (const auto it = sessions_.find(session_id); it != sessions_.end()) {
                runtime.peer_session = it->second.peer_session;
                sessions_.erase(it);
            }
        }

        if (runtime.peer_session != nullptr) {
            runtime.peer_session->Close();
        }

        protocol::common::WriteInfoLine("主机端会话结束 <- " + message.dump());
        return;
    }

    if (type == "error") {
        protocol::common::WriteErrorLine("主机端收到信令错误负载 <- " + message.dump());
        return;
    }

    protocol::common::WriteInfoLine("主机端忽略信令负载 <- " + message.dump());
}

/**
 * @brief 发送 JSON 消息。
 * @param message 待处理的消息对象。
 */
void HostClient::SendJson(const Json& message) {
    signal_client_.SendJson(message);
}

/**
 * @brief 停止相关流程。
 */
void HostClient::Stop() {
    {
        std::scoped_lock lock(mutex_);
        if (stop_requested_) {
            return;
        }

        stop_requested_ = true;
    }

    cv_.notify_all();
}

}  // namespace rdc
