/**
 * @file gui_main.hpp
 * @brief 声明 ui/gui_main 相关的类型、函数与流程。
 */

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <d3d12.h>
#include <dxgi1_4.h>
#include <windows.h>
#include <wrl/client.h>

#include "imgui.h"
#include "imgui_impl_dx12.h"
#include "widgets/ui_widgets.hpp"

namespace rdc::ui::detail {

using Microsoft::WRL::ComPtr;
using ::rdc::ui::widgets::BitmapIcon;

inline constexpr std::size_t kFramesInFlight = 2;
inline constexpr std::size_t kBackBufferCount = 2;
inline constexpr UINT kSrvHeapSize = 64;
inline constexpr DXGI_FORMAT kRenderTargetFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

/**
 * @brief 定义应用当前显示的页面。
 */
enum class NavigationPage {
    Home,
    Settings
};

/**
 * @brief 定义控制页访问时使用的页面协议。
 */
enum class ControllerPageScheme : std::size_t {
    Http = 0,
    Https = 1
};

/**
 * @brief 描述 GUI 首页当前的连接生命周期状态。
 */
enum class ManagedConnectionState {
    Disconnected,
    Starting,
    Connected
};

/**
 * @brief 描述 GUI 主循环待执行的连接动作。
 */
enum class PendingConnectionAction {
    None,
    Start,
    Stop
};

/**
 * @brief 保存 GUI 首页的输入状态与反馈信息。
 */
struct GuiState {
    std::array<char, 128> host_ip{};
    std::array<char, 128> controller_user_id{};
    std::array<char, 128> target_device_id{};
    std::array<char, 16> signal_port{};
    std::string status_message = "等待连接";
    std::string launched_controller_origin{};
    ControllerPageScheme page_scheme = ControllerPageScheme::Http;
    ManagedConnectionState connection_state = ManagedConnectionState::Disconnected;
    NavigationPage current_page = NavigationPage::Home;
};

/**
 * @brief 保存单帧命令分配器与对应的栅栏值。
 */
struct FrameContext {
    ComPtr<ID3D12CommandAllocator> command_allocator;
    UINT64 fence_value = 0;
};

/**
 * @brief 保存一个由 GUI 托管的后台子进程句柄。
 */
struct ManagedChildProcess {
    HANDLE process_handle = nullptr;
    DWORD process_id = 0;

    /**
     * @brief 判断当前子进程句柄是否有效。
     * @return 返回是否成功或条件是否满足。
     */
    [[nodiscard]] bool IsValid() const noexcept {
        return process_handle != nullptr;
    }
};

/**
 * @brief 将定长字符缓冲区转换为字符串视图。
 * @tparam BufferSize 缓冲区大小。
 * @param buffer 待读取的字符缓冲区。
 * @return 返回对应的字符串视图。
 */
template <std::size_t BufferSize>
std::string_view ReadBufferText(const std::array<char, BufferSize>& buffer) {
    const auto end = std::find(buffer.begin(), buffer.end(), '\0');
    const auto length = static_cast<std::size_t>(std::distance(buffer.begin(), end));
    return std::string_view(buffer.data(), length);
}

/**
 * @brief 将文本写入定长字符缓冲区。
 * @tparam BufferSize 缓冲区大小。
 * @param buffer 目标字符缓冲区。
 * @param text 待写入的 UTF-8 文本。
 */
template <std::size_t BufferSize>
void WriteBufferText(std::array<char, BufferSize>& buffer, std::string_view text) {
    buffer.fill('\0');
    const std::size_t copy_size = std::min(BufferSize - 1, text.size());
    if (copy_size > 0) {
        std::memcpy(buffer.data(), text.data(), copy_size);
    }
}

/**
 * @brief 尝试从内存字体候选集合中加载第一项可用字体。
 * @tparam CandidateCount 候选字体数量。
 * @param io ImGui IO 对象。
 * @param candidates 候选字体字节流集合。
 * @param font_size 字体大小。
 * @param glyph_ranges 字形范围。
 * @return 返回可用字体对象；失败时返回空指针。
 */
template <std::size_t CandidateCount>
ImFont* TryLoadFirstAvailableMemoryFont(ImGuiIO& io,
                                        const std::array<std::span<const unsigned char>, CandidateCount>& candidates,
                                        const float font_size,
                                        const ImWchar* glyph_ranges) {
    ImFontConfig font_config{};
    font_config.OversampleH = 2;
    font_config.OversampleV = 2;
    font_config.PixelSnapH = false;
    font_config.FontDataOwnedByAtlas = false;

    for (const std::span<const unsigned char> candidate : candidates) {
        if (candidate.empty()) {
            continue;
        }

        if (ImFont* font = io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(candidate.data()),
                                                          static_cast<int>(candidate.size()),
                                                          font_size,
                                                          &font_config,
                                                          glyph_ranges);
            font != nullptr) {
            return font;
        }
    }

    return nullptr;
}

/**
 * @brief 重置指定 COM 指针数组中的全部资源。
 * @tparam T COM 接口类型。
 * @tparam Count 数组元素数量。
 * @param resources 待重置的资源数组。
 */
template <typename T, std::size_t Count>
void ResetComArray(std::array<ComPtr<T>, Count>& resources) {
    for (auto& resource : resources) {
        resource.Reset();
    }
}

/**
 * @brief 判断字符是否允许直接出现在 URL 中。
 * @param ch 待判断字符。
 * @return 返回是否成功或条件是否满足。
 */
bool IsUrlSafeAscii(unsigned char ch);

/**
 * @brief 对 URL 参数文本执行百分号编码。
 * @param text 待编码文本。
 * @return 返回编码后的字符串结果。
 */
std::string UrlEncode(std::string_view text);

/**
 * @brief 将 UTF-8 文本转换为 UTF-16 字符串。
 * @param text 待转换文本。
 * @return 返回对应的 UTF-16 字符串。
 */
std::wstring ToWide(std::string_view text);

/**
 * @brief 获取页面协议枚举对应的协议文本。
 * @param scheme 页面协议枚举值。
 * @return 返回对应的协议文本。
 */
std::string_view GetControllerPageSchemeText(ControllerPageScheme scheme);

/**
 * @brief 检测当前主机优先使用的 IPv4 地址。
 * @return 返回检测到的主机 IPv4；失败时返回空值。
 */
std::optional<std::string> ResolvePreferredHostIpv4Address();

/**
 * @brief 构造资源状态切换屏障。
 * @param resource 目标资源。
 * @param before 切换前状态。
 * @param after 切换后状态。
 * @return 返回对应的屏障对象。
 */
D3D12_RESOURCE_BARRIER MakeTransitionBarrier(ID3D12Resource* resource,
                                             D3D12_RESOURCE_STATES before,
                                             D3D12_RESOURCE_STATES after);

/**
 * @brief 为 ImGui 的纹理 SRV 分配器维护一个简单空闲链。
 */
class DescriptorHeapAllocator {
public:
    /**
     * @brief 绑定 D3D12 设备与目标描述符堆。
     * @param device D3D12 设备。
     * @param heap 目标描述符堆。
     * @return 返回是否成功或条件是否满足。
     */
    bool Create(ID3D12Device* device, ID3D12DescriptorHeap* heap);

    /**
     * @brief 清空当前分配器持有的堆状态。
     */
    void Destroy();

    /**
     * @brief 分配一对 CPU/GPU 描述符句柄。
     * @param out_cpu_desc_handle 输出 CPU 描述符句柄。
     * @param out_gpu_desc_handle 输出 GPU 描述符句柄。
     * @return 返回是否成功或条件是否满足。
     */
    bool Allocate(D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu_desc_handle,
                  D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_desc_handle);

    /**
     * @brief 归还一对描述符句柄到空闲链。
     * @param cpu_desc_handle 归还的 CPU 描述符句柄。
     * @param gpu_desc_handle 归还的 GPU 描述符句柄。
     */
    void Free(D3D12_CPU_DESCRIPTOR_HANDLE cpu_desc_handle,
              D3D12_GPU_DESCRIPTOR_HANDLE gpu_desc_handle);

private:
    ID3D12DescriptorHeap* heap_ = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE heap_start_cpu_{};
    D3D12_GPU_DESCRIPTOR_HANDLE heap_start_gpu_{};
    UINT handle_increment_ = 0;
    std::vector<UINT> free_indices_{};
};

/**
 * @brief 封装基于 ImGui 的 Windows GUI 应用。
 */
class GuiApplication {
public:
    /**
     * @brief 执行 GUI 应用生命周期。
     * @return 返回状态码或退出码。
     */
    int Run();

private:
    /**
     * @brief 初始化窗口、D3D12 和 ImGui 运行环境。
     * @return 返回是否成功或条件是否满足。
     */
    bool Initialize();

    /**
     * @brief 释放 GUI 生命周期内创建的资源。
     */
    void Shutdown();

    /**
     * @brief 为首页填充默认字段值。
     */
    void InitializeDefaultState();

    /**
     * @brief 初始化 ImGui 字体集合。
     * @param io ImGui IO 对象。
     */
    void InitializeFonts(ImGuiIO& io);

    /**
     * @brief 加载左侧导航栏所需的位图图标资源。
     * @return 返回是否成功或条件是否满足。
     */
    bool LoadSidebarIcons();

    /**
     * @brief 释放左侧导航栏创建的位图图标资源。
     */
    void CleanupSidebarIcons();

    /**
     * @brief 从资源段读取单个位图图标并上传到 GPU。
     * @param resource_id 位图资源编号。
     * @param out_icon 输出位图图标对象。
     * @return 返回是否成功或条件是否满足。
     */
    bool LoadBitmapIcon(int resource_id, BitmapIcon& out_icon);

    /**
     * @brief 将位图像素数据上传为 D3D12 纹理。
     * @param width 位图宽度。
     * @param height 位图高度。
     * @param pixels 像素数据，格式为 BGRA8。
     * @param out_icon 输出位图图标对象。
     * @return 返回是否成功或条件是否满足。
     */
    bool CreateBitmapIconTexture(UINT width,
                                 UINT height,
                                 std::span<const std::uint32_t> pixels,
                                 BitmapIcon& out_icon);

    /**
     * @brief 释放单个位图图标占用的描述符与纹理资源。
     * @param icon 待释放图标。
     */
    void ReleaseBitmapIcon(BitmapIcon& icon);

    /**
     * @brief 应用 GUI 整体视觉主题。
     */
    void ApplyStyle();

    /**
     * @brief 创建主窗口并注册窗口类。
     * @return 返回是否成功或条件是否满足。
     */
    bool CreateMainWindow();

    /**
     * @brief 创建 D3D12 设备、交换链与命令对象。
     * @return 返回是否成功或条件是否满足。
     */
    bool CreateDeviceD3D();

    /**
     * @brief 释放当前 D3D12 相关资源。
     */
    void CleanupDeviceD3D();

    /**
     * @brief 创建 RTV 与 SRV 描述符堆。
     * @return 返回是否成功或条件是否满足。
     */
    bool CreateDescriptorHeaps();

    /**
     * @brief 创建命令队列、命令分配器、命令列表与同步对象。
     * @return 返回是否成功或条件是否满足。
     */
    bool CreateCommandObjects();

    /**
     * @brief 创建交换链并启用帧延迟等待对象。
     * @return 返回是否成功或条件是否满足。
     */
    bool CreateSwapChain();

    /**
     * @brief 为交换链后备缓冲区创建 RTV。
     * @return 返回是否成功或条件是否满足。
     */
    bool CreateRenderTargets();

    /**
     * @brief 释放当前交换链后备缓冲区资源。
     */
    void CleanupRenderTargets();

    /**
     * @brief 等待 GPU 完成已提交的所有命令。
     */
    void WaitForPendingOperations();

    /**
     * @brief 等待可用帧上下文并返回对应对象。
     * @return 返回下一帧可用的上下文。
     */
    FrameContext* WaitForNextFrameContext();

    /**
     * @brief 记录当前帧提交到命令队列后的栅栏值。
     * @param frame_context 当前帧上下文。
     * @return 返回是否成功或条件是否满足。
     */
    bool SignalFrameSubmission(FrameContext& frame_context);

    /**
     * @brief 根据当前 ImGui 绘制数据完成一次帧渲染与呈现。
     * @return 返回是否成功或条件是否满足。
     */
    bool RenderFrame();

    /**
     * @brief 按最新窗口尺寸重建交换链缓冲区。
     * @param width 新窗口宽度。
     * @param height 新窗口高度。
     * @return 返回是否成功或条件是否满足。
     */
    bool ResizeSwapChain(UINT width, UINT height);

    /**
     * @brief 执行主消息循环与逐帧渲染。
     */
    void MainLoop();

    /**
     * @brief 渲染整个 GUI 根界面。
     */
    void RenderUserInterface();

    /**
     * @brief 绘制左侧导航栏区域。
     */
    void DrawSidebarPanel();

    /**
     * @brief 绘制主内容面板区域。
     */
    void DrawContentPanel();

    /**
     * @brief 绘制首页布局与表单内容。
     */
    void DrawHomePage();

    /**
     * @brief 绘制设置页表单内容。
     */
    void DrawSettingsPage();

    /**
     * @brief 处理主循环中挂起的连接动作。
     */
    void ProcessPendingConnectionAction();

    /**
     * @brief 请求在后续帧中启动后台服务并打开控制页。
     */
    void RequestStartConnection();

    /**
     * @brief 请求在后续帧中断开并关闭后台服务。
     */
    void RequestStopConnection();

    /**
     * @brief 启动后台信令服务、主机端并打开控制页。
     * @return 返回是否成功或条件是否满足。
     */
    bool StartManagedConnection();

    /**
     * @brief 停止当前由 GUI 托管的后台进程并重置连接展示状态。
     * @param status_message 断开后展示的状态文本。
     */
    void StopManagedConnection(std::string_view status_message);

    /**
     * @brief 检查托管进程是否仍在运行，并在异常退出时回收状态。
     */
    void RefreshManagedConnectionState();

    /**
     * @brief 启动后台信令服务进程。
     * @return 返回是否成功或条件是否满足。
     */
    bool LaunchServerProcess();

    /**
     * @brief 启动后台主机端进程。
     * @return 返回是否成功或条件是否满足。
     */
    bool LaunchHostProcess();

    /**
     * @brief 创建当前连接使用的后台进程作业组。
     * @return 返回是否成功或条件是否满足。
     */
    bool EnsureManagedProcessJob();

    /**
     * @brief 终止并释放当前连接持有的后台进程作业组。
     */
    void CloseManagedProcessGroup();

    /**
     * @brief 以隐藏窗口方式启动一个受管子进程。
     * @param command_line 子进程完整命令行。
     * @param process_name 用于状态提示的进程名称。
     * @param out_process 输出受管子进程句柄。
     * @return 返回是否成功或条件是否满足。
     */
    bool LaunchChildProcess(const std::wstring& command_line,
                            std::string_view process_name,
                            ManagedChildProcess& out_process);

    /**
     * @brief 等待刚启动的子进程进入稳定运行状态。
     * @param process 待检查的受管进程。
     * @param process_name 用于状态提示的进程名称。
     * @return 返回是否成功或条件是否满足。
     */
    bool WaitForChildProcessStartup(const ManagedChildProcess& process, std::string_view process_name);

    /**
     * @brief 检查指定子进程当前是否仍在运行。
     * @param process 待检查的受管进程。
     * @return 返回是否成功或条件是否满足。
     */
    [[nodiscard]] bool IsChildProcessRunning(const ManagedChildProcess& process) const;

    /**
     * @brief 终止并关闭一个受管子进程句柄。
     * @param process 待停止的受管进程。
     */
    void CloseChildProcess(ManagedChildProcess& process);

    /**
     * @brief 仅释放一个受管子进程的句柄，不再附带终止动作。
     * @param process 待释放句柄的受管进程。
     */
    void ReleaseChildProcessHandle(ManagedChildProcess& process);

    /**
     * @brief 读取当前配置的信令端口。
     * @return 返回可用结果；失败时返回空值。
     */
    std::optional<std::uint16_t> ReadSignalPort() const;

    /**
     * @brief 构造当前表单对应的信令 WebSocket 地址。
     * @return 返回可用结果；失败时返回空值。
     */
    std::optional<std::string> BuildSignalUrl() const;

    /**
     * @brief 构造浏览器控制页 URL。
     * @return 返回可用结果；失败时返回空值。
     */
    std::optional<std::string> BuildControllerPageUrl() const;

    /**
     * @brief 构造浏览器控制页对应的站点根地址。
     * @return 返回可用结果；失败时返回空值。
     */
    std::optional<std::string> BuildControllerOrigin() const;

    /**
     * @brief 在默认浏览器中打开控制页。
     * @return 返回是否成功或条件是否满足。
     */
    bool OpenControllerPage();

    /**
     * @brief 处理本窗口收到的 Win32 消息。
     * @param message 消息编号。
     * @param w_param 第一个消息参数。
     * @param l_param 第二个消息参数。
     * @return 返回消息处理结果。
     */
    LRESULT HandleWindowMessage(UINT message, WPARAM w_param, LPARAM l_param);

    /**
     * @brief 为 ImGui DX12 后端分配 SRV 描述符。
     * @param init_info ImGui DX12 初始化参数。
     * @param out_cpu_desc_handle 输出 CPU 描述符句柄。
     * @param out_gpu_desc_handle 输出 GPU 描述符句柄。
     */
    static void AllocateSrvDescriptor(ImGui_ImplDX12_InitInfo* init_info,
                                      D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu_desc_handle,
                                      D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_desc_handle);

    /**
     * @brief 归还 ImGui DX12 后端使用的 SRV 描述符。
     * @param init_info ImGui DX12 初始化参数。
     * @param cpu_desc_handle 归还的 CPU 描述符句柄。
     * @param gpu_desc_handle 归还的 GPU 描述符句柄。
     */
    static void FreeSrvDescriptor(ImGui_ImplDX12_InitInfo* init_info,
                                  D3D12_CPU_DESCRIPTOR_HANDLE cpu_desc_handle,
                                  D3D12_GPU_DESCRIPTOR_HANDLE gpu_desc_handle);

    /**
     * @brief 作为 Win32 窗口过程回调转发消息。
     * @param window_handle 当前窗口句柄。
     * @param message 消息编号。
     * @param w_param 第一个消息参数。
     * @param l_param 第二个消息参数。
     * @return 返回消息处理结果。
     */
    static LRESULT CALLBACK WindowProc(HWND window_handle,
                                       UINT message,
                                       WPARAM w_param,
                                       LPARAM l_param);

    GuiState state_{};
    HWND window_handle_ = nullptr;
    std::array<FrameContext, kFramesInFlight> frame_contexts_{};
    std::array<ComPtr<ID3D12Resource>, kBackBufferCount> render_target_resources_{};
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, kBackBufferCount> render_target_descriptors_{};
    ComPtr<ID3D12Device> device_{};
    ComPtr<ID3D12DescriptorHeap> rtv_descriptor_heap_{};
    ComPtr<ID3D12DescriptorHeap> srv_descriptor_heap_{};
    ComPtr<ID3D12CommandQueue> command_queue_{};
    ComPtr<ID3D12GraphicsCommandList> command_list_{};
    ComPtr<IDXGISwapChain3> swap_chain_{};
    ComPtr<ID3D12Fence> fence_{};
    DescriptorHeapAllocator srv_descriptor_allocator_{};
    HANDLE fence_event_ = nullptr;
    HANDLE swap_chain_waitable_object_ = nullptr;
    std::size_t frame_index_ = 0;
    UINT rtv_descriptor_size_ = 0;
    UINT64 fence_last_signaled_value_ = 0;
    UINT swap_chain_flags_ = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
    bool imgui_context_initialized_ = false;
    bool win32_backend_initialized_ = false;
    bool dx12_backend_initialized_ = false;
    bool is_minimized_ = false;
    bool swap_chain_occluded_ = false;
    bool window_class_registered_ = false;
    const wchar_t* window_class_name_ = L"RDCDesktopGuiWindow";
    NavigationPage animated_page_ = NavigationPage::Home;
    float content_reveal_progress_ = 1.0F;
    BitmapIcon home_icon_{};
    BitmapIcon settings_icon_{};
    PendingConnectionAction pending_connection_action_ = PendingConnectionAction::None;
    HANDLE managed_process_job_ = nullptr;
    ManagedChildProcess server_process_{};
    ManagedChildProcess host_process_{};
};

}  // namespace rdc::ui::detail

namespace rdc::ui {

/**
 * @brief 执行 Windows GUI 主入口并返回退出码。
 * @return 返回状态码或退出码。
 */
int RunMain();

}  // namespace rdc::ui
