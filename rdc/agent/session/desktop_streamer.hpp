/**
 * @file desktop_streamer.hpp
 * @brief 声明 agent/session/desktop_streamer 相关的类型、函数与流程。
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

namespace rdc {

/**
 * @brief 封装 PeerSession 相关的会话状态与行为。
 */
class PeerSession;

namespace agent::session {

/**
 * @brief 描述 DesktopStreamerConfig 的配置项。
 */
struct DesktopStreamerConfig {
    std::uint32_t output_index = 0;
    std::uint32_t capture_timeout_ms = 100;
    std::uint32_t fps_num = 30;
    std::uint32_t fps_den = 1;
    std::uint32_t bitrate = 4'000'000;
    std::uint32_t gop_size = 60;
};

/**
 * @brief 封装桌面采集、编码与视频分发流程。
 */
class DesktopStreamer {
public:
    using ActiveSessionProvider = std::function<std::vector<std::shared_ptr<PeerSession>>()>;

    /**
     * @brief 构造 DesktopStreamer 对象。
     * @param active_session_provider activesessionprovider。
     * @param config 配置对象。
     */
    DesktopStreamer(ActiveSessionProvider active_session_provider, DesktopStreamerConfig config = {});
    /**
     * @brief 析构 DesktopStreamer 对象并释放相关资源。
     */
    ~DesktopStreamer();

    /**
     * @brief 启动相关流程。
     */
    void Start();
    /**
     * @brief 停止相关流程。
     */
    void Stop();

private:
    /**
     * @brief 运行Loop。
     */
    void RunLoop();

    ActiveSessionProvider active_session_provider_;
    DesktopStreamerConfig config_;
    std::atomic_bool stop_requested_{false};
    std::thread worker_;
};

}  // namespace agent::session
}  // namespace rdc
