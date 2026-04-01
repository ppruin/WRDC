/**
 * @file desktop_window_renderer.cpp
 * @brief 实现 controller/renderer/desktop_window_renderer 相关的类型、函数与流程。
 */

#include "desktop_window_renderer.hpp"

#include <stdexcept>

namespace rdc::controller::renderer {

namespace {

#ifdef _WIN32
constexpr wchar_t kWindowClassName[] = L"RdcDesktopWindowRenderer";
#endif

/**
 * @brief 执行 桌面窗口渲染器 相关处理。
 * @param title 窗口标题。
 */
}  // namespace

DesktopWindowRenderer::DesktopWindowRenderer(std::wstring title, CloseHandler close_handler)
    : title_(std::move(title)),
      close_handler_(std::move(close_handler)) {
}

/**
 * @brief 析构 DesktopWindowRenderer 对象并释放相关资源。
 */
DesktopWindowRenderer::~DesktopWindowRenderer() {
    Stop();
}

/**
 * @brief 启动相关流程。
 */
void DesktopWindowRenderer::Start() {
    std::unique_lock lock(mutex_);
    if (started_) {
        return;
    }

    stop_requested_ = false;
    started_ = true;
    thread_ = std::thread([this] {
        ThreadMain();
    });

    cv_.wait(lock, [this] {
        return window_ready_ || !started_;
    });
}

/**
 * @brief 停止相关流程。
 */
void DesktopWindowRenderer::Stop() {
    {
        std::scoped_lock lock(mutex_);
        if (!started_) {
            return;
        }

        stop_requested_ = true;
#ifdef _WIN32
        if (hwnd_ != nullptr) {
            PostMessageW(hwnd_, WM_CLOSE, 0, 0);
        }
#endif
    }

    if (thread_.joinable()) {
        thread_.join();
    }
}

/**
 * @brief 缓存待显示的视频帧。
 * @param frame 视频帧对象。
 */
void DesktopWindowRenderer::StorePresentedFrame(const DecodedVideoFrame& frame) {
    pending_frame_ = frame;
    has_pending_frame_ = true;
}

/**
 * @brief 缓存待显示的视频帧。
 * @param frame 视频帧对象。
 */
void DesktopWindowRenderer::StorePresentedFrame(DecodedVideoFrame& frame) {
    pending_frame_.Swap(frame);
    has_pending_frame_ = true;
}

/**
 * @brief 缓存待显示的视频帧。
 * @param frame 视频帧对象。
 */
void DesktopWindowRenderer::StorePresentedFrame(DecodedVideoFrame&& frame) {
    pending_frame_ = std::move(frame);
    has_pending_frame_ = true;
}

#ifdef _WIN32
/**
 * @brief 运行窗口线程主循环。
 */
void DesktopWindowRenderer::ThreadMain() {
    WNDCLASSW window_class{};
    window_class.lpfnWndProc = &DesktopWindowRenderer::WindowProc;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.lpszClassName = kWindowClassName;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&window_class);

    HWND hwnd = CreateWindowExW(0,
                                kWindowClassName,
                                title_.c_str(),
                                WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT,
                                CW_USEDEFAULT,
                                1280,
                                720,
                                nullptr,
                                nullptr,
                                GetModuleHandleW(nullptr),
                                this);
    if (hwnd == nullptr) {
        std::scoped_lock lock(mutex_);
        started_ = false;
        cv_.notify_all();
        return;
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    {
        std::scoped_lock lock(mutex_);
        hwnd_ = hwnd;
        window_ready_ = true;
        cv_.notify_all();
    }

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    std::scoped_lock lock(mutex_);
    hwnd_ = nullptr;
    window_ready_ = false;
    started_ = false;
    cv_.notify_all();
}

/**
 * @brief 执行 窗口Proc 相关处理。
 * @param hwnd 窗口句柄。
 * @param message 待处理的消息对象。
 * @param wparam Windows 消息的 WPARAM 参数。
 * @param lparam Windows 消息的 LPARAM 参数。
 * @return 返回窗口消息处理结果。
 */
LRESULT CALLBACK DesktopWindowRenderer::WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_NCCREATE) {
        const auto* create_struct = reinterpret_cast<CREATESTRUCTW*>(lparam);
        auto* self = static_cast<DesktopWindowRenderer*>(create_struct->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }

    auto* self = reinterpret_cast<DesktopWindowRenderer*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self != nullptr) {
        return self->HandleWindowMessage(hwnd, message, wparam, lparam);
    }

    return DefWindowProcW(hwnd, message, wparam, lparam);
}

/**
 * @brief 处理窗口消息。
 * @param hwnd 窗口句柄。
 * @param message 待处理的消息对象。
 * @param wparam Windows 消息的 WPARAM 参数。
 * @param lparam Windows 消息的 LPARAM 参数。
 * @return 返回窗口消息处理结果。
 */
LRESULT DesktopWindowRenderer::HandleWindowMessage(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_CLOSE: {
        CloseHandler close_handler;
        bool notify_close = false;
        {
            std::scoped_lock lock(mutex_);
            notify_close = !stop_requested_;
            close_handler = close_handler_;
        }

        if (notify_close && close_handler) {
            close_handler();
        }

        DestroyWindow(hwnd);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(hwnd, &paint);
        DrawFrame(dc, paint.rcPaint);
        EndPaint(hwnd, &paint);
        return 0;
    }
    default:
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }
}

/**
 * @brief 绘制当前待显示的视频帧。
 * @param dc dc。
 * @param client_rect 窗口客户区矩形。
 */
void DesktopWindowRenderer::DrawFrame(HDC dc, const RECT& client_rect) {
    {
        std::scoped_lock lock(mutex_);
        if (has_pending_frame_) {
            paint_frame_.Swap(pending_frame_);
            has_pending_frame_ = false;
        }
    }

    const auto& frame = paint_frame_;
    if (frame.bgra_bytes.empty() || frame.width == 0 || frame.height == 0) {
        FillRect(dc, &client_rect, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        return;
    }

    BITMAPINFO bitmap_info{};
    bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmap_info.bmiHeader.biWidth = static_cast<LONG>(frame.width);
    bitmap_info.bmiHeader.biHeight = -static_cast<LONG>(frame.height);
    bitmap_info.bmiHeader.biPlanes = 1;
    bitmap_info.bmiHeader.biBitCount = 32;
    bitmap_info.bmiHeader.biCompression = BI_RGB;

    const int target_width = client_rect.right - client_rect.left;
    const int target_height = client_rect.bottom - client_rect.top;
    StretchDIBits(dc,
                  0,
                  0,
                  target_width,
                  target_height,
                  0,
                  0,
                  static_cast<int>(frame.width),
                  static_cast<int>(frame.height),
                  frame.bgra_bytes.data(),
                  &bitmap_info,
                  DIB_RGB_COLORS,
                  SRCCOPY);
}
#endif

}  // namespace rdc::controller::renderer
