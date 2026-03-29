/**
 * @file desktop_streamer.cpp
 * @brief 实现 agent/session/desktop_streamer 相关的类型、函数与流程。
 */

#include "desktop_streamer.hpp"

#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#include "../encoder/bgra_to_nv12_converter.hpp"
#include "../platform/windows/d3d11_desktop_frame_reader.hpp"
#include "../platform/windows/dxgi_desktop_duplicator.hpp"
#include "../platform/windows/x264_h264_encoder.hpp"
#include "../rtc/peer_session.hpp"
#include "../../protocol/common/buffer_utils.hpp"
#include "../../protocol/common/console_logger.hpp"

namespace rdc::agent::session {

namespace {

#ifdef _WIN32
/**
 * @brief 封装 InputDesktopScope 作用域内的资源管理。
 */
class InputDesktopScope {
public:
    /**
     * @brief 构造 InputDesktopScope 对象。
     */
    InputDesktopScope() {
        desktop_ = OpenInputDesktop(0, FALSE, GENERIC_ALL);
        if (desktop_ != nullptr) {
            attached_ = SetThreadDesktop(desktop_) != FALSE;
        }
    }

    /**
     * @brief 析构 InputDesktopScope 对象并释放相关资源。
     */
    ~InputDesktopScope() {
        if (desktop_ != nullptr) {
            CloseDesktop(desktop_);
        }
    }

    /**
     * @brief 构造 InputDesktopScope 对象。
     */
    InputDesktopScope(const InputDesktopScope&) = delete;
    InputDesktopScope& operator=(const InputDesktopScope&) = delete;

    /**
     * @brief 判断Attached是否满足条件。
     * @return 返回是否成功或条件是否满足。
     */
    bool IsAttached() const {
        return attached_;
    }

    /**
     * @brief 获取Last错误码。
     * @return 返回对应结果。
     */
    DWORD GetLastErrorCode() const {
        return attached_ || desktop_ != nullptr ? ERROR_SUCCESS : GetLastError();
    }

private:
    HDESK desktop_ = nullptr;
    bool attached_ = false;
};
#endif

/**
 * @brief 判断Retryable采集错误是否满足条件。
 * @param message 待处理的消息对象。
 * @return 返回是否成功或条件是否满足。
 */
bool IsRetryableCaptureError(const std::string_view message) {
    return message.find("DuplicateOutput failed") != std::string_view::npos ||
           message.find("AcquireNextFrame failed") != std::string_view::npos ||
           message.find("GetFrameMoveRects failed") != std::string_view::npos ||
           message.find("GetFrameDirtyRects failed") != std::string_view::npos;
}

/**
 * @brief 筛选当前可接收视频的活动会话。
 * @param sessions sessions。
 * @param keyframe_requested keyframerequested。
 * @return 返回结果集合。
 */
std::vector<std::shared_ptr<PeerSession>> FilterReadySessions(
    const std::vector<std::shared_ptr<PeerSession>>& sessions,
    bool& keyframe_requested) {
    std::vector<std::shared_ptr<PeerSession>> ready_sessions;
    ready_sessions.reserve(sessions.size());

    for (const auto& session : sessions) {
        if (session == nullptr || !session->IsVideoReady()) {
            continue;
        }

        if (session->ConsumePendingKeyframeRequest()) {
            keyframe_requested = true;
        }

        ready_sessions.push_back(session);
    }

    return ready_sessions;
}

/**
 * @brief 执行 桌面Streamer 相关处理。
 * @param active_session_provider activesessionprovider。
 * @param config 配置对象。
 */
}  // namespace

DesktopStreamer::DesktopStreamer(ActiveSessionProvider active_session_provider, DesktopStreamerConfig config)
    : active_session_provider_(std::move(active_session_provider)),
      config_(config) {
}

/**
 * @brief 析构 DesktopStreamer 对象并释放相关资源。
 */
DesktopStreamer::~DesktopStreamer() {
    Stop();
}

/**
 * @brief 启动相关流程。
 */
void DesktopStreamer::Start() {
    if (worker_.joinable()) {
        return;
    }

    stop_requested_ = false;
    worker_ = std::thread([this] {
        RunLoop();
    });
}

/**
 * @brief 停止相关流程。
 */
void DesktopStreamer::Stop() {
    stop_requested_ = true;
    if (worker_.joinable()) {
        worker_.join();
    }
}

/**
 * @brief 运行Loop。
 */
void DesktopStreamer::RunLoop() {
    std::unique_ptr<platform::windows::DxgiDesktopDuplicator> capturer;
    std::unique_ptr<platform::windows::D3D11DesktopFrameReader> frame_reader;
    std::unique_ptr<encoder::BgraToNv12Converter> nv12_converter;
    std::unique_ptr<platform::windows::X264H264Encoder> encoder;
    bool capture_warning_reported = false;
    bool startup_logged = false;
    std::size_t last_ready_session_count = static_cast<std::size_t>(-1);

#ifdef _WIN32
    InputDesktopScope input_desktop_scope;
    if (!input_desktop_scope.IsAttached()) {
        protocol::common::WriteErrorLine(
            "桌面推流器附加输入桌面失败，可能无法进行 DuplicateOutput (错误码=" +
            std::to_string(input_desktop_scope.GetLastErrorCode()) + ")");
    }
#endif

    while (!stop_requested_) {
        try {
            bool keyframe_requested = false;
            const auto all_sessions = active_session_provider_ != nullptr ? active_session_provider_()
                                                                         : std::vector<std::shared_ptr<PeerSession>>{};
            auto ready_sessions = FilterReadySessions(all_sessions, keyframe_requested);
            const auto ready_session_count = ready_sessions.size();

            if (ready_session_count == 0) {
                if (last_ready_session_count != 0) {
                    protocol::common::WriteInfoLine("当前没有可接收桌面视频的活动会话，暂停桌面采集与编码");
                    protocol::common::ResetAll(capturer, frame_reader, nv12_converter, encoder);
                    capture_warning_reported = false;
                    startup_logged = false;
                    last_ready_session_count = 0;
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                continue;
            }

            if (last_ready_session_count != ready_session_count) {
                protocol::common::WriteInfoLine("当前可接收桌面视频的活动会话数: " +
                                                std::to_string(ready_session_count) +
                                                "，开始或继续桌面采集");
                last_ready_session_count = ready_session_count;
            }

            if (capturer == nullptr) {
                capturer = std::make_unique<platform::windows::DxgiDesktopDuplicator>(config_.output_index);
                frame_reader = std::make_unique<platform::windows::D3D11DesktopFrameReader>();
                nv12_converter = std::make_unique<encoder::BgraToNv12Converter>();
                capture_warning_reported = false;
                startup_logged = false;
            }

            auto frame = capturer->CaptureNextFrame(std::chrono::milliseconds(config_.capture_timeout_ms));
            if (!frame.has_value()) {
                continue;
            }

            if (!startup_logged) {
                protocol::common::WriteInfoLine("桌面推流器已捕获首帧桌面图像: " +
                                                std::to_string(frame->width) + "x" +
                                                std::to_string(frame->height) +
                                                ", 脏矩形=" + std::to_string(frame->dirty_rects.size()) +
                                                ", 位移矩形=" + std::to_string(frame->move_rects.size()));
                startup_logged = true;
            }

            auto raw_frame = frame_reader->Read(*frame);
            auto nv12_frame = nv12_converter->Convert(raw_frame);

            if (encoder == nullptr) {
                encoder = std::make_unique<platform::windows::X264H264Encoder>(
                    platform::windows::H264EncoderConfig{
                        .width = nv12_frame.width,
                        .height = nv12_frame.height,
                        .fps_num = config_.fps_num,
                        .fps_den = config_.fps_den,
                        .bitrate = config_.bitrate,
                        .gop_size = config_.gop_size,
                    });
            }

            if (keyframe_requested) {
                encoder->RequestKeyframe();
            }

            encoder->EncodeEach(nv12_frame, [&ready_sessions](const auto& encoded_frame) {
                for (const auto& session : ready_sessions) {
                    if (session != nullptr) {
                        session->SendVideoFrame(encoded_frame);
                    }
                }
            });
        } catch (const std::exception& ex) {
            const std::string message = ex.what();
            if (!IsRetryableCaptureError(message)) {
                protocol::common::WriteErrorLine("桌面推流器已停止: " + message);
                break;
            }

            if (!capture_warning_reported) {
                protocol::common::WriteErrorLine("桌面推流器当前无法采集，准备重试: " + message);
                capture_warning_reported = true;
            }

            protocol::common::ResetAll(capturer, frame_reader, nv12_converter, encoder);
            startup_logged = false;
            std::this_thread::sleep_for(std::chrono::seconds(1));
        } catch (...) {
            protocol::common::WriteErrorLine("桌面推流器已停止: 未知异常");
            break;
        }
    }

    if (encoder != nullptr) {
        const auto ready_sessions = active_session_provider_ != nullptr ? active_session_provider_()
                                                                        : std::vector<std::shared_ptr<PeerSession>>{};
        encoder->DrainEach([&ready_sessions](const auto& encoded_frame) {
            for (const auto& session : ready_sessions) {
                if (session != nullptr) {
                    session->SendVideoFrame(encoded_frame);
                }
            }
        });
    }
}

}  // namespace rdc::agent::session
