/**
 * @file desktop_streamer.cpp
 * @brief 实现 agent/session/desktop_streamer 相关的类型、函数与流程。
 */

#include "desktop_streamer.hpp"

#include <chrono>
#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>
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
#include "../platform/windows/h264_video_encoder.hpp"
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
 * @brief 筛选当前仍可发送视频的活动会话，不消费关键帧请求。
 * @param sessions sessions。
 * @return 返回结果集合。
 */
std::vector<std::shared_ptr<PeerSession>> FilterSendReadySessions(
    const std::vector<std::shared_ptr<PeerSession>>& sessions) {
    std::vector<std::shared_ptr<PeerSession>> ready_sessions;
    ready_sessions.reserve(sessions.size());

    for (const auto& session : sessions) {
        if (session == nullptr || !session->IsVideoReady()) {
            continue;
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
    bool capture_warning_reported = false;
    bool startup_logged = false;
    std::size_t last_ready_session_count = static_cast<std::size_t>(-1);
    std::mutex pending_nv12_mutex;
    std::condition_variable pending_nv12_cv;
    std::shared_ptr<encoder::Nv12VideoFrame> pending_nv12_frame;
    std::uint64_t dropped_preencode_frames = 0;
    bool encode_stop_requested = false;
    std::atomic_bool keyframe_request_pending = false;
    std::exception_ptr encode_failure;
    std::mutex encode_failure_mutex;

    const auto set_encode_failure = [&encode_failure, &encode_failure_mutex](std::exception_ptr failure) {
        std::scoped_lock lock(encode_failure_mutex);
        if (encode_failure == nullptr) {
            encode_failure = std::move(failure);
        }
    };

    const auto take_encode_failure = [&encode_failure, &encode_failure_mutex]() -> std::exception_ptr {
        std::scoped_lock lock(encode_failure_mutex);
        return encode_failure;
    };

    const auto dispatch_encoded_frame = [this](const encoder::EncodedVideoFrame& encoded_frame) {
        if (encoded_frame.bytes.empty()) {
            return;
        }

        const auto all_sessions = active_session_provider_ != nullptr ? active_session_provider_()
                                                                      : std::vector<std::shared_ptr<PeerSession>>{};
        auto ready_sessions = FilterSendReadySessions(all_sessions);
        if (ready_sessions.empty()) {
            return;
        }

        auto shared_frame = std::make_shared<encoder::EncodedVideoFrame>(encoded_frame);
        for (const auto& session : ready_sessions) {
            if (session != nullptr) {
                session->EnqueueVideoFrame(shared_frame);
            }
        }
    };

    std::thread encode_thread([this,
                               &config = config_,
                               &pending_nv12_mutex,
                               &pending_nv12_cv,
                               &pending_nv12_frame,
                               &encode_stop_requested,
                               &keyframe_request_pending,
                               &dispatch_encoded_frame,
                               &set_encode_failure]() {
        std::unique_ptr<platform::windows::H264VideoEncoder> encoder;
        std::uint32_t encoder_width = 0;
        std::uint32_t encoder_height = 0;

        try {
            while (true) {
                std::shared_ptr<encoder::Nv12VideoFrame> frame_to_encode;

                {
                    std::unique_lock lock(pending_nv12_mutex);
                    pending_nv12_cv.wait(lock, [&pending_nv12_frame, &encode_stop_requested] {
                        return encode_stop_requested || pending_nv12_frame != nullptr;
                    });

                    if (pending_nv12_frame == nullptr) {
                        if (encode_stop_requested) {
                            break;
                        }

                        continue;
                    }

                    frame_to_encode = std::move(pending_nv12_frame);
                    pending_nv12_frame.reset();
                }

                if (frame_to_encode == nullptr) {
                    continue;
                }

                if (encoder == nullptr ||
                    encoder_width != frame_to_encode->width ||
                    encoder_height != frame_to_encode->height) {
                    encoder = std::make_unique<platform::windows::H264VideoEncoder>(
                        platform::windows::H264EncoderConfig{
                            .width = frame_to_encode->width,
                            .height = frame_to_encode->height,
                            .fps_num = config.fps_num,
                            .fps_den = config.fps_den,
                            .bitrate = config.bitrate,
                            .gop_size = config.gop_size,
                            .backend = config.encoder_backend,
                        });
                    encoder_width = frame_to_encode->width;
                    encoder_height = frame_to_encode->height;
                    protocol::common::WriteInfoLine("桌面推流器已创建 H.264 编码器: backend=" +
                                                    std::string(encoder->ActiveBackendName()) +
                                                    ", 分辨率=" + std::to_string(encoder_width) + "x" +
                                                    std::to_string(encoder_height));
                }

                if (keyframe_request_pending.exchange(false)) {
                    encoder->RequestKeyframe();
                }

                encoder->EncodeEach(*frame_to_encode, [&dispatch_encoded_frame](const auto& encoded_frame) {
                    dispatch_encoded_frame(encoded_frame);
                });
            }

            if (encoder != nullptr) {
                encoder->DrainEach([&dispatch_encoded_frame](const auto& encoded_frame) {
                    dispatch_encoded_frame(encoded_frame);
                });
            }
        } catch (...) {
            set_encode_failure(std::current_exception());
            stop_requested_ = true;
            pending_nv12_cv.notify_all();
        }
    });

#ifdef _WIN32
    InputDesktopScope input_desktop_scope;
    if (!input_desktop_scope.IsAttached()) {
        protocol::common::WriteErrorLine(
            "桌面推流器附加输入桌面失败，可能无法进行 DuplicateOutput (错误码=" +
            std::to_string(input_desktop_scope.GetLastErrorCode()) + ")");
    }
#endif

    while (!stop_requested_) {
        if (const auto failure = take_encode_failure(); failure != nullptr) {
            try {
                std::rethrow_exception(failure);
            } catch (const std::exception& ex) {
                protocol::common::WriteErrorLine("桌面推流器编码线程已停止: " + std::string(ex.what()));
            } catch (...) {
                protocol::common::WriteErrorLine("桌面推流器编码线程已停止: 未知异常");
            }

            break;
        }

        try {
            bool keyframe_requested = false;
            const auto all_sessions = active_session_provider_ != nullptr ? active_session_provider_()
                                                                         : std::vector<std::shared_ptr<PeerSession>>{};
            auto ready_sessions = FilterReadySessions(all_sessions, keyframe_requested);
            const auto ready_session_count = ready_sessions.size();

            if (ready_session_count == 0) {
                if (last_ready_session_count != 0) {
                    protocol::common::WriteInfoLine("当前没有可接收桌面视频的活动会话，暂停桌面采集与编码");
                    protocol::common::ResetAll(capturer, frame_reader, nv12_converter);
                    capture_warning_reported = false;
                    startup_logged = false;
                    last_ready_session_count = 0;
                    keyframe_request_pending = false;
                    dropped_preencode_frames = 0;

                    {
                        std::scoped_lock lock(pending_nv12_mutex);
                        pending_nv12_frame.reset();
                    }
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

            if (keyframe_requested) {
                keyframe_request_pending = true;
            }

            bool dropped_previous_frame = false;
            std::uint64_t dropped_frame_count = 0;

            {
                std::scoped_lock lock(pending_nv12_mutex);
                dropped_previous_frame = pending_nv12_frame != nullptr;
                if (dropped_previous_frame) {
                    dropped_frame_count = ++dropped_preencode_frames;
                }

                pending_nv12_frame = std::make_shared<encoder::Nv12VideoFrame>(std::move(nv12_frame));
            }

            pending_nv12_cv.notify_one();

            if (dropped_previous_frame &&
                (dropped_frame_count <= 5 || dropped_frame_count % 60 == 0)) {
                protocol::common::WriteInfoLine(
                    "桌面推流器编码前队列已丢弃旧帧以保持低时延, 累计丢弃=" +
                    std::to_string(dropped_frame_count) +
                    ", 累积帧数=" + std::to_string(frame->accumulated_frames));
            }

            if (frame->accumulated_frames > 1 &&
                (frame->accumulated_frames <= 5 || frame->accumulated_frames % 60 == 0)) {
                protocol::common::WriteInfoLine(
                    "桌面采集已出现积压, DXGI 累积帧数=" +
                    std::to_string(frame->accumulated_frames));
                }
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

            protocol::common::ResetAll(capturer, frame_reader, nv12_converter);
            startup_logged = false;
            std::this_thread::sleep_for(std::chrono::seconds(1));
        } catch (...) {
            protocol::common::WriteErrorLine("桌面推流器已停止: 未知异常");
            break;
        }
    }

    {
        std::scoped_lock lock(pending_nv12_mutex);
        encode_stop_requested = true;
    }

    pending_nv12_cv.notify_all();

    if (encode_thread.joinable()) {
        encode_thread.join();
    }

    if (const auto failure = take_encode_failure(); failure != nullptr) {
        try {
            std::rethrow_exception(failure);
        } catch (const std::exception& ex) {
            protocol::common::WriteErrorLine("桌面推流器编码线程已停止: " + std::string(ex.what()));
        } catch (...) {
            protocol::common::WriteErrorLine("桌面推流器编码线程已停止: 未知异常");
        }
    }
}

}  // namespace rdc::agent::session
