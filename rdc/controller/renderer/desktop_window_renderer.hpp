/**
 * @file desktop_window_renderer.hpp
 * @brief 声明 controller/renderer/desktop_window_renderer 相关的类型、函数与流程。
 */

#pragma once

#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#endif

#include "decoded_video_frame.hpp"

namespace rdc::controller::renderer {

/**
 * @brief 封装 DesktopWindowRenderer 相关的桌面窗口显示流程。
 */
class DesktopWindowRenderer {
public:
    /**
     * @brief 定义窗口关闭时触发的回调类型。
     */
    using CloseHandler = std::function<void()>;

    /**
     * @brief 构造 DesktopWindowRenderer 对象。
     * @param title 窗口标题。
     * @param close_handler 窗口关闭回调。
     */
    explicit DesktopWindowRenderer(std::wstring title, CloseHandler close_handler = {});
    /**
     * @brief 析构 DesktopWindowRenderer 对象并释放相关资源。
     */
    ~DesktopWindowRenderer();

    /**
     * @brief 构造 DesktopWindowRenderer 对象。
     */
    DesktopWindowRenderer(const DesktopWindowRenderer&) = delete;
    DesktopWindowRenderer& operator=(const DesktopWindowRenderer&) = delete;

    /**
     * @brief 启动相关流程。
     */
    void Start();
    /**
     * @brief 停止相关流程。
     */
    void Stop();

    /**
     * @brief 执行 Present 相关处理。
     * @param frame 视频帧对象。
     */
    template <typename Frame>
    void Present(Frame&& frame) {
        std::scoped_lock lock(mutex_);
        StorePresentedFrame(std::forward<Frame>(frame));
#ifdef _WIN32
        if (hwnd_ != nullptr) {
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
#endif
    }

private:
#ifdef _WIN32
    /**
     * @brief 执行 窗口Proc 相关处理。
     * @param hwnd 窗口句柄。
     * @param message 待处理的消息对象。
     * @param wparam Windows 消息的 WPARAM 参数。
     * @param lparam Windows 消息的 LPARAM 参数。
     * @return 返回窗口消息处理结果。
     */
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    /**
     * @brief 处理窗口消息。
     * @param hwnd 窗口句柄。
     * @param message 待处理的消息对象。
     * @param wparam Windows 消息的 WPARAM 参数。
     * @param lparam Windows 消息的 LPARAM 参数。
     * @return 返回窗口消息处理结果。
     */
    LRESULT HandleWindowMessage(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    /**
     * @brief 运行窗口线程主循环。
     */
    void ThreadMain();
    /**
     * @brief 绘制当前待显示的视频帧。
     * @param dc dc。
     * @param client_rect 窗口客户区矩形。
     */
    void DrawFrame(HDC dc, const RECT& client_rect);
#endif

    /**
     * @brief 缓存待显示的视频帧。
     * @param frame 视频帧对象。
     */
    void StorePresentedFrame(const DecodedVideoFrame& frame);
    /**
     * @brief 缓存待显示的视频帧。
     * @param frame 视频帧对象。
     */
    void StorePresentedFrame(DecodedVideoFrame& frame);
    /**
     * @brief 缓存待显示的视频帧。
     * @param frame 视频帧对象。
     */
    void StorePresentedFrame(DecodedVideoFrame&& frame);

    std::wstring title_;
    CloseHandler close_handler_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::thread thread_;
    DecodedVideoFrame pending_frame_;
    DecodedVideoFrame paint_frame_;
    bool started_ = false;
    bool window_ready_ = false;
    bool stop_requested_ = false;
    bool has_pending_frame_ = false;
#ifdef _WIN32
    HWND hwnd_ = nullptr;
#endif
};

}  // namespace rdc::controller::renderer
