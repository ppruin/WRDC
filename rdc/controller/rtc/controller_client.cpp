/**
 * @file controller_client.cpp
 * @brief 实现 controller/rtc/controller_client 相关的类型、函数与流程。
 */

#include "controller_client.hpp"

#include <chrono>
#include <cstdlib>
#include <optional>
#include <thread>
#include <type_traits>
#include <utility>

#include "../../protocol/common/buffer_utils.hpp"
#include "../../protocol/common/console_logger.hpp"

namespace rdc {

namespace {

/**
 * @brief 描述 RtpPacketView 视图。
 */
struct RtpPacketView {
    const std::uint8_t* payload = nullptr;
    std::size_t payload_size = 0;
    bool marker = false;
};

/**
 * @brief 解析环境变量无符号整数。
 * @param name 名称字符串。
 * @param fallback 回退值。
 * @return 返回对应结果。
 */
template <typename Unsigned>
Unsigned ParseEnvUnsigned(const char* name, const Unsigned fallback) {
    static_assert(std::is_unsigned_v<Unsigned>, "ParseEnvUnsigned 仅支持无符号整数类型");

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
 * @brief 读取环境变量标记。
 * @param name 名称字符串。
 * @param fallback 回退值。
 * @return 返回是否成功或条件是否满足。
 */
bool ReadEnvFlag(const char* name, const bool fallback) {
    if (const char* value = std::getenv(name); value != nullptr) {
        const std::string text = value;
        if (text == "1" || text == "true" || text == "TRUE" || text == "yes" || text == "on") {
            return true;
        }

        if (text == "0" || text == "false" || text == "FALSE" || text == "no" || text == "off") {
            return false;
        }
    }

    return fallback;
}

/**
 * @brief 解析RTPPacket。
 * @param data 输入数据或缓冲区指针。
 * @param size 字节长度。
 * @return 返回可用结果；失败时返回空值。
 */
std::optional<RtpPacketView> ParseRtpPacket(const std::uint8_t* data, const std::size_t size) {
    if (data == nullptr || size < 12) {
        return std::nullopt;
    }

    const auto version = static_cast<std::uint8_t>(data[0] >> 6);
    if (version != 2) {
        return std::nullopt;
    }

    const bool has_padding = (data[0] & 0x20U) != 0;
    const bool has_extension = (data[0] & 0x10U) != 0;
    const auto csrc_count = static_cast<std::size_t>(data[0] & 0x0FU);
    std::size_t header_size = 12 + csrc_count * 4;
    if (size < header_size) {
        return std::nullopt;
    }

    if (has_extension) {
        if (size < header_size + 4) {
            return std::nullopt;
        }

        const auto extension_length_words =
            static_cast<std::uint16_t>((static_cast<std::uint16_t>(data[header_size + 2]) << 8) |
                                       static_cast<std::uint16_t>(data[header_size + 3]));
        header_size += 4 + static_cast<std::size_t>(extension_length_words) * 4;
        if (size < header_size) {
            return std::nullopt;
        }
    }

    std::size_t payload_size = size - header_size;
    if (has_padding) {
        const auto padding_size = static_cast<std::size_t>(data[size - 1]);
        if (padding_size == 0 || padding_size > payload_size) {
            return std::nullopt;
        }

        payload_size -= padding_size;
    }

    if (payload_size == 0) {
        return std::nullopt;
    }

    return RtpPacketView{
        .payload = data + header_size,
        .payload_size = payload_size,
        .marker = (data[1] & 0x80U) != 0,
    };
}

/**
 * @brief 执行 控制端客户端 相关处理。
 * @param signal_url 信令服务地址。
 * @param user_id 控制端用户标识。
 */
}  // namespace

ControllerClient::ControllerClient(std::string signal_url, std::string user_id, std::string target_device_id)
    : signal_url_(std::move(signal_url)),
      user_id_(std::move(user_id)),
      target_device_id_(std::move(target_device_id)) {
}

/**
 * @brief 运行相关流程。
 * @return 返回状态码或退出码。
 */
int ControllerClient::Run() {
    ConfigureSocket();

    protocol::common::WriteInfoLine("控制端正在连接 " + signal_url_ + "，用户 ID: " + user_id_ +
                                    "，目标设备 ID: " + target_device_id_);
    signal_client_.Connect(signal_url_);

    SendJson(Json{
        {"type", protocol::kRegisterController},
        {"userId", user_id_}
    });

    std::unique_lock lock(mutex_);
    cv_.wait(lock, [this] {
        return stop_requested_;
    });
    lock.unlock();

    if (session_ != nullptr) {
        session_->Close();
        session_.reset();
    }

    signal_client_.Close();

    return 0;
}

/**
 * @brief 配置套接字。
 */
void ControllerClient::ConfigureSocket() {
    signal_client_.SetClosedHandler([this]() {
        protocol::common::WriteInfoLine("控制端信令连接已关闭");
        Stop();
    });

    signal_client_.SetErrorHandler([](const std::string& error) {
        protocol::common::WriteErrorLine("控制端信令错误: " + error);
    });

    signal_client_.SetMessageHandler([this](const Json& message) {
        HandleMessage(message);
    });
}

/**
 * @brief 处理消息。
 * @param message 待处理的消息对象。
 */
void ControllerClient::HandleMessage(const Json& message) {
    const std::string type = message.value("type", "");

    if (type == "hello") {
        protocol::common::WriteInfoLine("控制端收到信令 <- " + message.dump());
        return;
    }

    if (type == "registered") {
        protocol::common::WriteInfoLine("控制端注册完成，开始创建会话");
        SendJson(Json{
            {"type", protocol::kCreateSession},
            {"targetDeviceId", target_device_id_}
        });
        return;
    }

    if (type == "session_created") {
        session_id_ = message.value("sessionId", "");
        protocol::common::WriteInfoLine("控制端会话已创建: " + session_id_);
        return;
    }

    if (type == "session_accepted") {
        if (session_id_.empty()) {
            session_id_ = message.value("sessionId", "");
        }

        if (session_ == nullptr) {
            first_video_access_unit_received_ = false;
            received_video_access_units_ = 0;
            {
                std::scoped_lock lock(video_packet_mutex_);
                video_depacketizer_.Reset();
                pending_access_unit_.clear();
            }

            session_ = std::make_shared<PeerSession>(
                PeerRole::Controller,
                session_id_,
                [this](const Json& payload) {
                    SendJson(Json{
                        {"type", protocol::kSignal},
                        {"sessionId", session_id_},
                        {"payload", payload}
                    });
                },
                [this](std::string_view channel_label, const Json& payload) {
                    protocol::common::WriteInfoLine("控制端数据 <- [" + std::string(channel_label) + "] " + payload.dump());

                    if (channel_label == "control" && payload.value("type", "") == "pong") {
                        const bool close_on_pong = ReadEnvFlag("RDC_CONTROLLER_CLOSE_ON_PONG", false);
                        if (!close_on_pong) {
                            protocol::common::WriteInfoLine("控制端已收到 pong，保持会话以继续接收远端视频数据");
                            return;
                        }

                        const auto close_delay_ms = ParseEnvUnsigned<std::uint32_t>("RDC_CONTROLLER_CLOSE_DELAY_MS", 0);
                        if (close_delay_ms > 0) {
                            protocol::common::WriteInfoLine("控制端延迟 " + std::to_string(close_delay_ms) + "ms 后关闭会话");
                            std::this_thread::sleep_for(std::chrono::milliseconds(close_delay_ms));
                        }

                        SendJson(Json{
                            {"type", protocol::kCloseSession},
                            {"sessionId", session_id_},
                            {"reason", "ping_completed"}
                        });
                        Stop();
                    }
                },
                [this](const std::uint8_t* data, const std::size_t size) {
                    HandleVideoSample(data, size);
                });

            session_->Start();
        }

        return;
    }

    if (type == protocol::kSignal) {
        if (session_ != nullptr) {
            session_->HandleSignal(message.value("payload", Json::object()));
        }

        return;
    }

    if (type == "session_closed" || type == "session_failed" || type == "session_rejected") {
        protocol::common::WriteInfoLine("控制端会话结束 <- " + message.dump());
        if (session_ != nullptr) {
            session_->Close();
            session_.reset();
        }

        {
            std::scoped_lock lock(video_packet_mutex_);
            video_depacketizer_.Reset();
            pending_access_unit_.clear();
        }

        Stop();
        return;
    }

    if (type == "error") {
        protocol::common::WriteErrorLine("控制端收到信令错误负载 <- " + message.dump());
        Stop();
        return;
    }

    protocol::common::WriteInfoLine("控制端忽略信令负载 <- " + message.dump());
}

/**
 * @brief 发送 JSON 消息。
 * @param message 待处理的消息对象。
 */
void ControllerClient::SendJson(const Json& message) {
    signal_client_.SendJson(message);
}

/**
 * @brief 构建访问单元。
 * @param data 输入数据或缓冲区指针。
 * @param size 字节长度。
 * @return 返回可用结果；失败时返回空值。
 */
std::optional<ControllerClient::AnnexBNalu> ControllerClient::BuildAccessUnit(const std::uint8_t* data,
                                                                              const std::size_t size) {
    const auto packet = ParseRtpPacket(data, size);
    if (!packet.has_value()) {
        return std::nullopt;
    }

    std::scoped_lock lock(video_packet_mutex_);
    video_depacketizer_.ForEachAnnexBNalu(packet->payload, packet->payload_size, [this](AnnexBNalu nalu) {
        protocol::common::AppendRange(pending_access_unit_, nalu.begin(), nalu.end());
    });

    if (!packet->marker || pending_access_unit_.empty()) {
        return std::nullopt;
    }

    auto access_unit = std::move(pending_access_unit_);
    pending_access_unit_.clear();
    return access_unit;
}

/**
 * @brief 处理视频样本。
 * @param data 输入数据或缓冲区指针。
 * @param size 字节长度。
 */
void ControllerClient::HandleVideoSample(const std::uint8_t* data, const std::size_t size) {
    auto access_unit = BuildAccessUnit(data, size);
    if (!access_unit.has_value()) {
        return;
    }

    const auto access_unit_index = ++received_video_access_units_;
    if (!first_video_access_unit_received_.exchange(true)) {
        protocol::common::WriteInfoLine("控制端已收到首个完整远端视频访问单元, 字节数=" +
                                        std::to_string(access_unit->size()));
        return;
    }

    if (access_unit_index % 120 == 0) {
        protocol::common::WriteInfoLine("控制端持续接收远端视频访问单元, 序号=" +
                                        std::to_string(access_unit_index) +
                                        ", 字节数=" + std::to_string(access_unit->size()));
    }
}

/**
 * @brief 停止相关流程。
 */
void ControllerClient::Stop() {
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
