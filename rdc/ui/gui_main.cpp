/**
 * @file gui_main.cpp
 * @brief 实现 ui/gui_main 相关的类型、函数与流程。
 */

#include "gui_main.hpp"

#include "animations/ui_animations.hpp"

#include <charconv>
#include <chrono>
#include <cstdlib>
#include <iphlpapi.h>
#include <limits>
#include <shellapi.h>
#include <thread>

#include "../resource.h"
#include "../read_resource.h"
#include "imgui_impl_win32.h"

#pragma comment(lib, "iphlpapi.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd,
                                                             UINT msg,
                                                             WPARAM wParam,
                                                             LPARAM lParam);

namespace rdc::ui::detail {

namespace {

/**
 * @brief 保存从 Windows 资源段读取的字体字节流。
 */
std::vector<unsigned char> g_font_data{};

/**
 * @brief 定义侧边栏位图图标使用的纹理格式。
 */
inline constexpr DXGI_FORMAT kBitmapIconFormat = DXGI_FORMAT_B8G8R8A8_UNORM;

/**
 * @brief 定义控制页协议选择框的候选项文本。
 */
inline constexpr std::array<const char*, 2> kControllerPageSchemeLabels{"http", "https"};

/**
 * @brief 定义后台子进程启动稳定性检查的轮询次数。
 */
inline constexpr int kChildProcessStartupPollCount = 10;

/**
 * @brief 定义后台子进程启动稳定性检查的轮询间隔。
 */
inline constexpr auto kChildProcessStartupPollInterval = std::chrono::milliseconds(50);

/**
 * @brief 对单个命令行参数执行 Windows 风格引用。
 * @param argument 待编码的命令行参数。
 * @return 返回可直接拼接进命令行的参数文本。
 */
std::wstring QuoteCommandLineArgument(const std::wstring_view argument) {
    if (argument.empty()) {
        return L"\"\"";
    }

    if (argument.find_first_of(L" \t\"") == std::wstring_view::npos) {
        return std::wstring(argument);
    }

    std::wstring quoted_argument;
    quoted_argument.reserve(argument.size() + 2);
    quoted_argument.push_back(L'"');
    for (const wchar_t ch : argument) {
        if (ch == L'"') {
            quoted_argument += L"\\\"";
            continue;
        }

        quoted_argument.push_back(ch);
    }

    quoted_argument.push_back(L'"');
    return quoted_argument;
}

/**
 * @brief 构造后台子进程完整命令行。
 * @tparam ArgumentCount 额外参数数量。
 * @param executable_path 当前程序可执行文件路径。
 * @param mode 子进程启动模式。
 * @param arguments 额外参数集合。
 * @return 返回拼接完成的命令行文本。
 */
template <std::size_t ArgumentCount>
std::wstring BuildChildProcessCommandLine(
    const std::wstring_view executable_path,
    const std::wstring_view mode,
    const std::array<std::wstring_view, ArgumentCount>& arguments) {
    std::wstring command_line = QuoteCommandLineArgument(executable_path);
    command_line.push_back(L' ');
    command_line += QuoteCommandLineArgument(mode);

    for (const std::wstring_view argument : arguments) {
        command_line.push_back(L' ');
        command_line += QuoteCommandLineArgument(argument);
    }

    return command_line;
}

/**
 * @brief 获取当前可执行文件的绝对路径。
 * @return 返回可执行文件路径；失败时返回空值。
 */
std::optional<std::wstring> GetCurrentExecutablePath() {
    std::array<wchar_t, MAX_PATH> path_buffer{};
    const DWORD path_length =
        ::GetModuleFileNameW(nullptr, path_buffer.data(), static_cast<DWORD>(path_buffer.size()));
    if (path_length == 0 || path_length >= path_buffer.size()) {
        return std::nullopt;
    }

    return std::wstring(path_buffer.data(), path_length);
}

/**
 * @brief 判断当前环境是否已配置 HTTPS 证书与私钥。
 * @return 返回是否成功或条件是否满足。
 */
bool HasHttpsEnvironmentConfiguration() {
    const char* const tls_cert = std::getenv("RDC_SIGNAL_CERT");
    const char* const tls_key = std::getenv("RDC_SIGNAL_KEY");
    return tls_cert != nullptr && tls_cert[0] != '\0' && tls_key != nullptr && tls_key[0] != '\0';
}

/**
 * @brief 解析端口输入框中的端口值。
 * @param text 端口文本。
 * @return 返回可用端口；失败时返回空值。
 */
std::optional<std::uint16_t> ParsePortText(const std::string_view text) {
    if (text.empty()) {
        return std::nullopt;
    }

    std::uint32_t parsed_port = 0;
    const char* const begin = text.data();
    const char* const end = text.data() + text.size();
    const auto [ptr, ec] = std::from_chars(begin, end, parsed_port);
    if (ec != std::errc{} || ptr != end || parsed_port == 0U ||
        parsed_port > std::numeric_limits<std::uint16_t>::max()) {
        return std::nullopt;
    }

    return static_cast<std::uint16_t>(parsed_port);
}

}  // namespace

bool IsUrlSafeAscii(const unsigned char ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
           ch == '-' || ch == '_' || ch == '.' || ch == '~';
}

/**
 * @brief 获取页面协议枚举对应的协议文本。
 * @param scheme 页面协议枚举值。
 * @return 返回对应的协议文本。
 */
std::string_view GetControllerPageSchemeText(const ControllerPageScheme scheme) {
    return kControllerPageSchemeLabels[static_cast<std::size_t>(scheme)];
}

/**
 * @brief 检测当前主机优先使用的 IPv4 地址。
 * @return 返回检测到的主机 IPv4；失败时返回空值。
 */
std::optional<std::string> ResolvePreferredHostIpv4Address() {
    ULONG buffer_size = 0;
    if (::GetAdaptersInfo(nullptr, &buffer_size) != ERROR_BUFFER_OVERFLOW || buffer_size == 0) {
        return std::nullopt;
    }

    std::vector<unsigned char> adapter_buffer(buffer_size);
    auto* adapters = reinterpret_cast<IP_ADAPTER_INFO*>(adapter_buffer.data());
    if (::GetAdaptersInfo(adapters, &buffer_size) != NO_ERROR) {
        return std::nullopt;
    }

    const auto select_ipv4_from_adapters =
        [&](const bool require_gateway) -> std::optional<std::string> {
        for (const IP_ADAPTER_INFO* adapter = adapters; adapter != nullptr;
             adapter = adapter->Next) {
            if (adapter->Type == MIB_IF_TYPE_LOOPBACK) {
                continue;
            }

            const std::string gateway = adapter->GatewayList.IpAddress.String;
            if (require_gateway && (gateway.empty() || gateway == "0.0.0.0")) {
                continue;
            }

            const std::string ipv4_address = adapter->IpAddressList.IpAddress.String;
            if (ipv4_address.empty() || ipv4_address == "0.0.0.0" ||
                ipv4_address.rfind("127.", 0) == 0 ||
                ipv4_address.rfind("169.254.", 0) == 0) {
                continue;
            }

            return ipv4_address;
        }

        return std::nullopt;
    };

    if (std::optional<std::string> preferred_address = select_ipv4_from_adapters(true);
        preferred_address.has_value()) {
        return preferred_address;
    }

    return select_ipv4_from_adapters(false);
}

/**
 * @brief 对 URL 参数文本执行百分号编码。
 * @param text 待编码文本。
 * @return 返回编码后的字符串结果。
 */
std::string UrlEncode(const std::string_view text) {
    static constexpr char kHexDigits[] = "0123456789ABCDEF";

    std::string encoded;
    encoded.reserve(text.size() * 3);
    for (const unsigned char ch : text) {
        if (IsUrlSafeAscii(ch)) {
            encoded.push_back(static_cast<char>(ch));
            continue;
        }

        encoded.push_back('%');
        encoded.push_back(kHexDigits[(ch >> 4U) & 0x0FU]);
        encoded.push_back(kHexDigits[ch & 0x0FU]);
    }

    return encoded;
}

/**
 * @brief 将 UTF-8 文本转换为 UTF-16 字符串。
 * @param text 待转换文本。
 * @return 返回对应的 UTF-16 字符串。
 */
std::wstring ToWide(const std::string_view text) {
    if (text.empty()) {
        return {};
    }

    const int required_size = MultiByteToWideChar(CP_UTF8,
                                                  0,
                                                  text.data(),
                                                  static_cast<int>(text.size()),
                                                  nullptr,
                                                  0);
    if (required_size <= 0) {
        return {};
    }

    std::wstring result(static_cast<std::size_t>(required_size), L'\0');
    const int converted_size = MultiByteToWideChar(CP_UTF8,
                                                   0,
                                                   text.data(),
                                                   static_cast<int>(text.size()),
                                                   result.data(),
                                                   required_size);
    if (converted_size <= 0) {
        return {};
    }

    return result;
}

/**
 * @brief 构造资源状态切换屏障。
 * @param resource 目标资源。
 * @param before 切换前状态。
 * @param after 切换后状态。
 * @return 返回对应的屏障对象。
 */
D3D12_RESOURCE_BARRIER MakeTransitionBarrier(ID3D12Resource* resource,
                                             const D3D12_RESOURCE_STATES before,
                                             const D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    return barrier;
}

	bool DescriptorHeapAllocator::Create(ID3D12Device* device, ID3D12DescriptorHeap* heap) 
	{
	    if (device == nullptr || heap == nullptr)
	        return false;
	    
	    Destroy();
	    heap_ = heap;

	    const D3D12_DESCRIPTOR_HEAP_DESC description = heap->GetDesc();
	    heap_start_cpu_ = heap->GetCPUDescriptorHandleForHeapStart();
	    heap_start_gpu_ = heap->GetGPUDescriptorHandleForHeapStart();
	    handle_increment_ = device->GetDescriptorHandleIncrementSize(description.Type);

	    free_indices_.reserve(description.NumDescriptors);

	    for (UINT index = description.NumDescriptors; index > 0; --index) 
        {
	        free_indices_.push_back(index - 1);
	    }

	    return true;
	}

	void DescriptorHeapAllocator::Destroy() 
	{
	    heap_ = nullptr;
	    heap_start_cpu_ = {};
	    heap_start_gpu_ = {};
	    handle_increment_ = 0;
	    free_indices_.clear();
	}

	bool DescriptorHeapAllocator::Allocate(D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu_desc_handle,
	                                       D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_desc_handle) 
	{
	    if (heap_ == nullptr || out_cpu_desc_handle == nullptr || out_gpu_desc_handle == nullptr ||
	        free_indices_.empty()) 
	    {
	        return false;
	    }

	    const UINT index = free_indices_.back();
	    free_indices_.pop_back();

	    out_cpu_desc_handle->ptr = heap_start_cpu_.ptr + static_cast<SIZE_T>(index) * handle_increment_;
	    out_gpu_desc_handle->ptr = heap_start_gpu_.ptr + static_cast<UINT64>(index) * handle_increment_;
	    return true;
	}

	void DescriptorHeapAllocator::Free(const D3D12_CPU_DESCRIPTOR_HANDLE cpu_desc_handle,
	                                   const D3D12_GPU_DESCRIPTOR_HANDLE gpu_desc_handle) 
	{
	    if (heap_ == nullptr || handle_increment_ == 0)
	        return;

	    const auto cpu_index = static_cast<UINT>((cpu_desc_handle.ptr - heap_start_cpu_.ptr) / handle_increment_);
	    const auto gpu_index = static_cast<UINT>((gpu_desc_handle.ptr - heap_start_gpu_.ptr) / handle_increment_);

	    if (cpu_index != gpu_index)
	        return;

	    free_indices_.push_back(cpu_index);
	}

	int GuiApplication::Run() 
	{
	    if (!Initialize()) 
        {
	        Shutdown();
	        return 1;
	    }

	    MainLoop();
	    Shutdown();
	    return 0;
	}

	bool GuiApplication::Initialize() 
	{
	    InitializeDefaultState();
	    ImGui_ImplWin32_EnableDpiAwareness();

	    if (!CreateMainWindow())
	        return false;

	    if (!CreateDeviceD3D()) 
	        return false;

	    IMGUI_CHECKVERSION();
	    ImGui::CreateContext();
	    imgui_context_initialized_ = true;

	    ImGuiIO& io = ImGui::GetIO();
	    io.IniFilename = nullptr;
	    io.LogFilename = nullptr;
	    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

	    ApplyStyle();
	    InitializeFonts(io);
        LoadSidebarIcons();

	    if (!ImGui_ImplWin32_Init(window_handle_)) 
        {
	        state_.status_message = "初始化 Win32 ImGui 后端失败";
	        return false;
	    }
	    win32_backend_initialized_ = true;

	    ImGui_ImplDX12_InitInfo init_info{};
	    init_info.Device = device_.Get();
	    init_info.CommandQueue = command_queue_.Get();
	    init_info.NumFramesInFlight = static_cast<int>(kFramesInFlight);
	    init_info.RTVFormat = kRenderTargetFormat;
	    init_info.DSVFormat = DXGI_FORMAT_UNKNOWN;
	    init_info.UserData = this;
	    init_info.SrvDescriptorHeap = srv_descriptor_heap_.Get();
	    init_info.SrvDescriptorAllocFn = &GuiApplication::AllocateSrvDescriptor;
	    init_info.SrvDescriptorFreeFn = &GuiApplication::FreeSrvDescriptor;

	    if (!ImGui_ImplDX12_Init(&init_info)) 
        {
	        state_.status_message = "初始化 D3D12 ImGui 后端失败";
	        return false;
	    }
	    dx12_backend_initialized_ = true;

	    ShowWindow(window_handle_, SW_SHOWDEFAULT);
	    UpdateWindow(window_handle_);
	    return true;
	}

	void GuiApplication::Shutdown() 
	{
        StopManagedConnection({});

	    if (dx12_backend_initialized_) 
        {
	        ImGui_ImplDX12_Shutdown();
	        dx12_backend_initialized_ = false;
	    }

	    if (win32_backend_initialized_) 
        {
	        ImGui_ImplWin32_Shutdown();
	        win32_backend_initialized_ = false;
	    }

	    if (imgui_context_initialized_) 
        {
	        ImGui::DestroyContext();
	        imgui_context_initialized_ = false;
	    }

	    CleanupDeviceD3D();

	    if (window_handle_ != nullptr) 
        {
	        DestroyWindow(window_handle_);
	        window_handle_ = nullptr;
	    }

	    if (window_class_registered_) 
        {
	        UnregisterClassW(window_class_name_, GetModuleHandleW(nullptr));
	        window_class_registered_ = false;
	    }
	}

	void GuiApplication::InitializeDefaultState() 
	{
	    WriteBufferText(state_.controller_user_id, "user-web-1");
	    WriteBufferText(state_.target_device_id, "host-1");
        WriteBufferText(state_.signal_port, "5000");
        state_.launched_controller_origin.clear();
        state_.page_scheme = ControllerPageScheme::Http;
        state_.connection_state = ManagedConnectionState::Disconnected;
        pending_connection_action_ = PendingConnectionAction::None;

        if (const std::optional<std::string> preferred_ipv4 = ResolvePreferredHostIpv4Address();
            preferred_ipv4.has_value()) {
            WriteBufferText(state_.host_ip, *preferred_ipv4);
            return;
        }

        WriteBufferText(state_.host_ip, "");
        state_.status_message = "未检测到可用的主机 IPv4 地址";
	}

void GuiApplication::InitializeFonts(ImGuiIO& io) {
    try {
        const HINSTANCE hinstance = ::GetModuleHandleW(nullptr);
        g_font_data = LoadBinaryResource(hinstance, IDR_FONT1, RT_RCDATA);
    } catch (const std::exception&) {
        g_font_data.clear();
    }

    const std::array<std::span<const unsigned char>, 1> font_candidates{
        std::span<const unsigned char>(g_font_data.data(), g_font_data.size()),
    };

    if (ImFont* font = TryLoadFirstAvailableMemoryFont(io,
                                                       font_candidates,
                                                       24.0F,
                                                       io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
        font != nullptr) {
        io.FontDefault = font;
        return;
    }

    state_.status_message = "字体资源加载失败，已回退默认字体";
    io.Fonts->AddFontDefault();
}

/**
 * @brief 加载左侧导航栏所需的位图图标资源。
 * @return 返回是否成功或条件是否满足。
 */
bool GuiApplication::LoadSidebarIcons() {
    const bool home_icon_loaded = LoadBitmapIcon(IDB_HOME_ICON, home_icon_);
    const bool settings_icon_loaded = LoadBitmapIcon(IDB_SETTINGS_ICON, settings_icon_);

    if ((!home_icon_loaded || !settings_icon_loaded) && state_.status_message == "等待连接") {
        state_.status_message = "BMP 图标资源加载失败，已回退文字按钮";
    }

    return home_icon_loaded && settings_icon_loaded;
}

/**
 * @brief 释放左侧导航栏创建的位图图标资源。
 */
void GuiApplication::CleanupSidebarIcons() {
    ReleaseBitmapIcon(home_icon_);
    ReleaseBitmapIcon(settings_icon_);
}

/**
 * @brief 从资源段读取单个位图图标并上传到 GPU。
 * @param resource_id 位图资源编号。
 * @param out_icon 输出位图图标对象。
 * @return 返回是否成功或条件是否满足。
 */
bool GuiApplication::LoadBitmapIcon(const int resource_id, BitmapIcon& out_icon) {
    std::vector<std::uint32_t> pixels;
    UINT width = 0;
    UINT height = 0;
    if (!widgets::LoadBitmapResourcePixels(::GetModuleHandleW(nullptr),
                                           resource_id,
                                           pixels,
                                           width,
                                           height)) {
        return false;
    }

    return CreateBitmapIconTexture(width, height, pixels, out_icon);
}

/**
 * @brief 将位图像素数据上传为 D3D12 纹理。
 * @param width 位图宽度。
 * @param height 位图高度。
 * @param pixels 像素数据，格式为 BGRA8。
 * @param out_icon 输出位图图标对象。
 * @return 返回是否成功或条件是否满足。
 */
bool GuiApplication::CreateBitmapIconTexture(const UINT width,
                                             const UINT height,
                                             const std::span<const std::uint32_t> pixels,
                                             BitmapIcon& out_icon) {
    if (device_ == nullptr || command_queue_ == nullptr || command_list_ == nullptr ||
        width == 0 || height == 0 ||
        pixels.size() < static_cast<std::size_t>(width) * static_cast<std::size_t>(height)) {
        return false;
    }

    BitmapIcon loaded_icon{};
    loaded_icon.width = width;
    loaded_icon.height = height;

    if (!srv_descriptor_allocator_.Allocate(&loaded_icon.cpu_descriptor, &loaded_icon.gpu_descriptor)) {
        return false;
    }

    const auto release_loaded_icon = [&]() {
        if (loaded_icon.cpu_descriptor.ptr != 0 && loaded_icon.gpu_descriptor.ptr != 0) {
            srv_descriptor_allocator_.Free(loaded_icon.cpu_descriptor, loaded_icon.gpu_descriptor);
        }

        loaded_icon = {};
    };

    D3D12_RESOURCE_DESC texture_description{};
    texture_description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texture_description.Alignment = 0;
    texture_description.Width = width;
    texture_description.Height = height;
    texture_description.DepthOrArraySize = 1;
    texture_description.MipLevels = 1;
    texture_description.Format = kBitmapIconFormat;
    texture_description.SampleDesc.Count = 1;
    texture_description.SampleDesc.Quality = 0;
    texture_description.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texture_description.Flags = D3D12_RESOURCE_FLAG_NONE;

    D3D12_HEAP_PROPERTIES default_heap_properties{};
    default_heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    default_heap_properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    default_heap_properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    default_heap_properties.CreationNodeMask = 1;
    default_heap_properties.VisibleNodeMask = 1;

    if (FAILED(device_->CreateCommittedResource(&default_heap_properties,
                                                D3D12_HEAP_FLAG_NONE,
                                                &texture_description,
                                                D3D12_RESOURCE_STATE_COPY_DEST,
                                                nullptr,
                                                IID_PPV_ARGS(loaded_icon.texture.GetAddressOf())))) {
        release_loaded_icon();
        return false;
    }

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT64 upload_buffer_size = 0;
    device_->GetCopyableFootprints(&texture_description,
                                   0,
                                   1,
                                   0,
                                   &footprint,
                                   nullptr,
                                   nullptr,
                                   &upload_buffer_size);

    D3D12_RESOURCE_DESC upload_buffer_description{};
    upload_buffer_description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    upload_buffer_description.Alignment = 0;
    upload_buffer_description.Width = upload_buffer_size;
    upload_buffer_description.Height = 1;
    upload_buffer_description.DepthOrArraySize = 1;
    upload_buffer_description.MipLevels = 1;
    upload_buffer_description.Format = DXGI_FORMAT_UNKNOWN;
    upload_buffer_description.SampleDesc.Count = 1;
    upload_buffer_description.SampleDesc.Quality = 0;
    upload_buffer_description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    upload_buffer_description.Flags = D3D12_RESOURCE_FLAG_NONE;

    D3D12_HEAP_PROPERTIES upload_heap_properties{};
    upload_heap_properties.Type = D3D12_HEAP_TYPE_UPLOAD;
    upload_heap_properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    upload_heap_properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    upload_heap_properties.CreationNodeMask = 1;
    upload_heap_properties.VisibleNodeMask = 1;

    ComPtr<ID3D12Resource> upload_buffer;
    if (FAILED(device_->CreateCommittedResource(&upload_heap_properties,
                                                D3D12_HEAP_FLAG_NONE,
                                                &upload_buffer_description,
                                                D3D12_RESOURCE_STATE_GENERIC_READ,
                                                nullptr,
                                                IID_PPV_ARGS(upload_buffer.GetAddressOf())))) {
        release_loaded_icon();
        return false;
    }

    void* mapped_buffer = nullptr;
    if (FAILED(upload_buffer->Map(0, nullptr, &mapped_buffer)) || mapped_buffer == nullptr) {
        release_loaded_icon();
        return false;
    }

    const std::size_t source_row_pitch =
        static_cast<std::size_t>(width) * sizeof(std::uint32_t);
    auto* destination_bytes = static_cast<std::byte*>(mapped_buffer);
    const auto* source_bytes = reinterpret_cast<const std::byte*>(pixels.data());
    for (UINT row_index = 0; row_index < height; ++row_index) {
        std::memcpy(destination_bytes + footprint.Offset +
                        static_cast<std::size_t>(row_index) * footprint.Footprint.RowPitch,
                    source_bytes + static_cast<std::size_t>(row_index) * source_row_pitch,
                    source_row_pitch);
    }
    upload_buffer->Unmap(0, nullptr);

    D3D12_SHADER_RESOURCE_VIEW_DESC shader_resource_view_description{};
    shader_resource_view_description.Format = kBitmapIconFormat;
    shader_resource_view_description.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    shader_resource_view_description.Shader4ComponentMapping =
        D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    shader_resource_view_description.Texture2D.MostDetailedMip = 0;
    shader_resource_view_description.Texture2D.MipLevels = 1;
    shader_resource_view_description.Texture2D.PlaneSlice = 0;
    shader_resource_view_description.Texture2D.ResourceMinLODClamp = 0.0F;
    device_->CreateShaderResourceView(loaded_icon.texture.Get(),
                                      &shader_resource_view_description,
                                      loaded_icon.cpu_descriptor);

    WaitForPendingOperations();

    FrameContext& upload_frame_context = frame_contexts_.front();
    if (FAILED(upload_frame_context.command_allocator->Reset()) ||
        FAILED(command_list_->Reset(upload_frame_context.command_allocator.Get(), nullptr))) {
        release_loaded_icon();
        return false;
    }

    D3D12_TEXTURE_COPY_LOCATION destination_location{};
    destination_location.pResource = loaded_icon.texture.Get();
    destination_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    destination_location.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION source_location{};
    source_location.pResource = upload_buffer.Get();
    source_location.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    source_location.PlacedFootprint = footprint;

    command_list_->CopyTextureRegion(&destination_location, 0, 0, 0, &source_location, nullptr);

    const D3D12_RESOURCE_BARRIER texture_barrier =
        MakeTransitionBarrier(loaded_icon.texture.Get(),
                              D3D12_RESOURCE_STATE_COPY_DEST,
                              D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    command_list_->ResourceBarrier(1, &texture_barrier);

    if (FAILED(command_list_->Close())) {
        release_loaded_icon();
        return false;
    }

    ID3D12CommandList* const command_lists[] = {command_list_.Get()};
    command_queue_->ExecuteCommandLists(1, command_lists);
    WaitForPendingOperations();

    ReleaseBitmapIcon(out_icon);
    out_icon = std::move(loaded_icon);
    return true;
}

/**
 * @brief 释放单个位图图标占用的描述符与纹理资源。
 * @param icon 待释放图标。
 */
void GuiApplication::ReleaseBitmapIcon(BitmapIcon& icon) {
    if (icon.cpu_descriptor.ptr != 0 && icon.gpu_descriptor.ptr != 0) {
        srv_descriptor_allocator_.Free(icon.cpu_descriptor, icon.gpu_descriptor);
    }

    icon = {};
}

	void GuiApplication::ApplyStyle() 
	{
	    ImGuiStyle& style = ImGui::GetStyle();
	    style.WindowPadding = ImVec2(14.0F, 14.0F);
	    style.FramePadding = ImVec2(18.0F, 10.0F);
	    style.ItemSpacing = ImVec2(12.0F, 16.0F);
	    style.WindowRounding = 18.0F;
	    style.FrameRounding = 14.0F;
	    style.ChildRounding = 18.0F;
	    style.PopupRounding = 16.0F;
	    style.ScrollbarRounding = 14.0F;
	    style.GrabRounding = 12.0F;
	    style.WindowBorderSize = 0.0F;
	    style.FrameBorderSize = 0.0F;
	    style.ChildBorderSize = 0.0F;

	    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.95F, 0.97F, 0.99F, 1.0F);
	    style.Colors[ImGuiCol_ChildBg] = ImVec4(0.95F, 0.97F, 0.99F, 0.0F);
	    style.Colors[ImGuiCol_Text] = ImVec4(0.10F, 0.12F, 0.15F, 1.0F);
	    style.Colors[ImGuiCol_Button] = ImVec4(0.38F, 0.75F, 0.91F, 1.0F);
	    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.35F, 0.72F, 0.89F, 1.0F);
	    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.32F, 0.68F, 0.84F, 1.0F);
	}

	bool GuiApplication::CreateMainWindow() 
	{
	    WNDCLASSEXW window_class{};
	    window_class.cbSize = sizeof(window_class);
	    window_class.style = CS_CLASSDC;
	    window_class.lpfnWndProc = &GuiApplication::WindowProc;
	    window_class.hInstance = GetModuleHandleW(nullptr);
	    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
	    window_class.lpszClassName = window_class_name_;

	    if (RegisterClassExW(&window_class) == 0) 
        {
	        state_.status_message = "注册 GUI 窗口类失败";
	        return false;
	    }

	    window_class_registered_ = true;
	    window_handle_ = CreateWindowW(window_class_name_,
	                                   L"RDC Desktop",
	                                   WS_OVERLAPPEDWINDOW,
	                                   CW_USEDEFAULT,
	                                   CW_USEDEFAULT,
	                                   1280,
	                                   760,
	                                   nullptr,
	                                   nullptr,
	                                   GetModuleHandleW(nullptr),
	                                   this);

	    if (window_handle_ == nullptr) 
        {
	        state_.status_message = "创建 GUI 主窗口失败";
	        return false;
	    }

	    return true;
	}

	bool GuiApplication::CreateDeviceD3D() 
	{
	    if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(device_.GetAddressOf())))) 
        {
	        state_.status_message = "D3D12 设备初始化失败";
	        return false;
	    }

	    if (!CreateDescriptorHeaps()) 
	        return false;

	    if (!CreateCommandObjects()) 
	        return false;

	    if (!CreateSwapChain()) 
	        return false;

	    if (!CreateRenderTargets())
	        return false;

	    return true;
	}

	void GuiApplication::CleanupDeviceD3D() 
	{
	    CleanupSidebarIcons();
	    CleanupRenderTargets();

	    if (swap_chain_ != nullptr) 
        {
	        swap_chain_->SetFullscreenState(FALSE, nullptr);
	        swap_chain_.Reset();
	    }

	    if (swap_chain_waitable_object_ != nullptr) 
        {
	        CloseHandle(swap_chain_waitable_object_);
	        swap_chain_waitable_object_ = nullptr;
	    }

	    command_list_.Reset();
	    command_queue_.Reset();
	    rtv_descriptor_heap_.Reset();
	    srv_descriptor_heap_.Reset();
	    fence_.Reset();
	    device_.Reset();
	    srv_descriptor_allocator_.Destroy();

	    for (auto& frame_context : frame_contexts_) 
        {
	        frame_context.command_allocator.Reset();
	        frame_context.fence_value = 0;
	    }

	    if (fence_event_ != nullptr) 
        {
	        CloseHandle(fence_event_);
	        fence_event_ = nullptr;
	    }

	    frame_index_ = 0;
	    rtv_descriptor_size_ = 0;
	    fence_last_signaled_value_ = 0;
	    is_minimized_ = false;
	    swap_chain_occluded_ = false;
	}

	bool GuiApplication::CreateDescriptorHeaps()
	{
	    D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_description{};
	    rtv_heap_description.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	    rtv_heap_description.NumDescriptors = static_cast<UINT>(kBackBufferCount);
	    rtv_heap_description.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

	    if (FAILED(device_->CreateDescriptorHeap(&rtv_heap_description,
	                                             IID_PPV_ARGS(rtv_descriptor_heap_.GetAddressOf())))) 
        {
	        state_.status_message = "创建 RTV 描述符堆失败";
	        return false;
	    }

	    rtv_descriptor_size_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	    D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle = rtv_descriptor_heap_->GetCPUDescriptorHandleForHeapStart();

	    for (std::size_t index = 0; index < kBackBufferCount; ++index) 
        {
	        render_target_descriptors_[index] = rtv_handle;
	        rtv_handle.ptr += rtv_descriptor_size_;
	    }

	    D3D12_DESCRIPTOR_HEAP_DESC srv_heap_description{};
	    srv_heap_description.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	    srv_heap_description.NumDescriptors = kSrvHeapSize;
	    srv_heap_description.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

	    if (FAILED(device_->CreateDescriptorHeap(&srv_heap_description,
	                                             IID_PPV_ARGS(srv_descriptor_heap_.GetAddressOf())))) 
        {
	        state_.status_message = "创建 SRV 描述符堆失败";
	        return false;
	    }

	    if (!srv_descriptor_allocator_.Create(device_.Get(), srv_descriptor_heap_.Get())) 
        {
	        state_.status_message = "初始化 SRV 描述符分配器失败";
	        return false;
	    }

	    return true;
	}

	bool GuiApplication::CreateCommandObjects() 
	{
	    D3D12_COMMAND_QUEUE_DESC queue_description{};
	    queue_description.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

	    if (FAILED(device_->CreateCommandQueue(&queue_description,
	                                           IID_PPV_ARGS(command_queue_.GetAddressOf())))) 
        {
	        state_.status_message = "创建 D3D12 命令队列失败";
	        return false;
	    }

	    for (auto& frame_context : frame_contexts_) 
        {
	        if (FAILED(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
	                                                   IID_PPV_ARGS(frame_context.command_allocator.GetAddressOf())))) 
            {
	            state_.status_message = "创建 D3D12 命令分配器失败";
	            return false;
	        }
	    }

	    if (FAILED(device_->CreateCommandList(0,
	                                          D3D12_COMMAND_LIST_TYPE_DIRECT,
	                                          frame_contexts_.front().command_allocator.Get(),
	                                          nullptr,
	                                          IID_PPV_ARGS(command_list_.GetAddressOf())))) 
        {
	        state_.status_message = "创建 D3D12 命令列表失败";
	        return false;
	    }

	    if (FAILED(command_list_->Close())) 
        {
	        state_.status_message = "初始化 D3D12 命令列表失败";
	        return false;
	    }

	    if (FAILED(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence_.GetAddressOf())))) 
        {
	        state_.status_message = "创建 D3D12 栅栏失败";
	        return false;
	    }

	    fence_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);

	    if (fence_event_ == nullptr) 
        {
	        state_.status_message = "创建 D3D12 栅栏事件失败";
	        return false;
	    }

	    return true;
	}

	bool GuiApplication::CreateSwapChain() 
	{
	    ComPtr<IDXGIFactory4> dxgi_factory;
	    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(dxgi_factory.GetAddressOf())))) 
		{
	        state_.status_message = "创建 DXGI Factory 失败";
	        return false;
	    }

	    DXGI_SWAP_CHAIN_DESC1 swap_chain_description{};
	    swap_chain_description.BufferCount = static_cast<UINT>(kBackBufferCount);
	    swap_chain_description.Width = 0;
	    swap_chain_description.Height = 0;
	    swap_chain_description.Format = kRenderTargetFormat;
	    swap_chain_description.Flags = swap_chain_flags_;
	    swap_chain_description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	    swap_chain_description.SampleDesc.Count = 1;
	    swap_chain_description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	    swap_chain_description.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
	    swap_chain_description.Scaling = DXGI_SCALING_STRETCH;
	    swap_chain_description.Stereo = FALSE;

	    ComPtr<IDXGISwapChain1> swap_chain;

	    if (FAILED(dxgi_factory->CreateSwapChainForHwnd(command_queue_.Get(),
	                                                    window_handle_,
	                                                    &swap_chain_description,
	                                                    nullptr,
	                                                    nullptr,
	                                                    swap_chain.GetAddressOf()))) 
		{
	        state_.status_message = "创建 D3D12 交换链失败";
	        return false;
	    }

	    if (FAILED(dxgi_factory->MakeWindowAssociation(window_handle_, DXGI_MWA_NO_ALT_ENTER))) 
		{
	        state_.status_message = "禁用 Alt+Enter 全屏切换失败";
	        return false;
	    }

	    if (FAILED(swap_chain.As(&swap_chain_))) 
		{
	        state_.status_message = "获取 IDXGISwapChain3 接口失败";
	        return false;
	    }

	    if (FAILED(swap_chain_->SetMaximumFrameLatency(static_cast<UINT>(kBackBufferCount)))) 
		{
	        state_.status_message = "设置交换链帧延迟失败";
	        return false;
	    }

	    swap_chain_waitable_object_ = swap_chain_->GetFrameLatencyWaitableObject();

	    if (swap_chain_waitable_object_ == nullptr) 
		{
	        state_.status_message = "获取交换链等待句柄失败";
	        return false;
	    }

	    return true;
	}

	bool GuiApplication::CreateRenderTargets() 
	{
	    for (std::size_t index = 0; index < kBackBufferCount; ++index) 
		{
	        if (FAILED(swap_chain_->GetBuffer(static_cast<UINT>(index),
	                                          IID_PPV_ARGS(render_target_resources_[index].ReleaseAndGetAddressOf())))) 
			{
	            state_.status_message = "获取 D3D12 后备缓冲区失败";
	            return false;
	        }

	        device_->CreateRenderTargetView(render_target_resources_[index].Get(),
	                                        nullptr,
	                                        render_target_descriptors_[index]);
	    }

	    return true;
	}

	void GuiApplication::CleanupRenderTargets() 
	{
	    WaitForPendingOperations();
	    ResetComArray(render_target_resources_);
	}

	void GuiApplication::WaitForPendingOperations() 
	{
	    if (command_queue_ == nullptr || fence_ == nullptr || fence_event_ == nullptr)
	        return;

	    const UINT64 fence_value = ++fence_last_signaled_value_;
	    if (FAILED(command_queue_->Signal(fence_.Get(), fence_value)))
	        return;

	    if (FAILED(fence_->SetEventOnCompletion(fence_value, fence_event_)))
	        return;

	    WaitForSingleObject(fence_event_, INFINITE);
	}

	FrameContext* GuiApplication::WaitForNextFrameContext()
	{
	    FrameContext& frame_context = frame_contexts_[frame_index_ % kFramesInFlight];

	    if (fence_ != nullptr && fence_->GetCompletedValue() < frame_context.fence_value)
		{
	        if (FAILED(fence_->SetEventOnCompletion(frame_context.fence_value, fence_event_)))
	            return &frame_context;

	        if (swap_chain_waitable_object_ != nullptr) 
			{
	            HANDLE waitable_objects[] = {
	                swap_chain_waitable_object_,
	                fence_event_,
	            };
	            WaitForMultipleObjects(static_cast<DWORD>(std::size(waitable_objects)),
	                                   waitable_objects,
	                                   TRUE,
	                                   INFINITE);
	        }
	    	else 
	    	{
	            WaitForSingleObject(fence_event_, INFINITE);
	        }
	    }
		else if (swap_chain_waitable_object_ != nullptr) 
		{
	        WaitForSingleObject(swap_chain_waitable_object_, INFINITE);
	    }

	    return &frame_context;
	}

	bool GuiApplication::SignalFrameSubmission(FrameContext& frame_context) 
	{
	    const UINT64 fence_value = ++fence_last_signaled_value_;

	    if (FAILED(command_queue_->Signal(fence_.Get(), fence_value))) 
		{
	        state_.status_message = "提交 D3D12 栅栏失败";
	        return false;
	    }

	    frame_context.fence_value = fence_value;
	    return true;
	}

	bool GuiApplication::RenderFrame() 
	{
	    if (swap_chain_ == nullptr || command_list_ == nullptr || command_queue_ == nullptr) 
		{
	        state_.status_message = "D3D12 渲染上下文尚未就绪";
	        return false;
	    }

	    FrameContext* frame_context = WaitForNextFrameContext();
	    if (frame_context == nullptr || frame_context->command_allocator == nullptr) 
		{
	        state_.status_message = "D3D12 帧上下文不可用";
	        return false;
	    }

	    const UINT back_buffer_index = swap_chain_->GetCurrentBackBufferIndex();
	    ID3D12Resource* const render_target_resource =
	        render_target_resources_[static_cast<std::size_t>(back_buffer_index)].Get();
	    if (render_target_resource == nullptr) 
		{
	        state_.status_message = "D3D12 后备缓冲区不可用";
	        return false;
	    }

	    if (FAILED(frame_context->command_allocator->Reset())) 
		{
	        state_.status_message = "重置 D3D12 命令分配器失败";
	        return false;
	    }

	    if (FAILED(command_list_->Reset(frame_context->command_allocator.Get(), nullptr))) 
		{
	        state_.status_message = "重置 D3D12 命令列表失败";
	        return false;
	    }

	    D3D12_RESOURCE_BARRIER barrier = MakeTransitionBarrier(render_target_resource,
	                                                           D3D12_RESOURCE_STATE_PRESENT,
	                                                           D3D12_RESOURCE_STATE_RENDER_TARGET);
	    command_list_->ResourceBarrier(1, &barrier);

	    constexpr float kClearColor[4] = {
	        0.95F,
	        0.97F,
	        0.99F,
	        1.0F,
	    };
	    command_list_->ClearRenderTargetView(render_target_descriptors_[static_cast<std::size_t>(back_buffer_index)],
	                                         kClearColor,
	                                         0,
	                                         nullptr);
	    command_list_->OMSetRenderTargets(1,
	                                      &render_target_descriptors_[static_cast<std::size_t>(back_buffer_index)],
	                                      FALSE,
	                                      nullptr);

	    ID3D12DescriptorHeap* descriptor_heaps[] = {srv_descriptor_heap_.Get()};
	    command_list_->SetDescriptorHeaps(static_cast<UINT>(std::size(descriptor_heaps)), descriptor_heaps);
	    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), command_list_.Get());

	    barrier = MakeTransitionBarrier(render_target_resource,
	                                    D3D12_RESOURCE_STATE_RENDER_TARGET,
	                                    D3D12_RESOURCE_STATE_PRESENT);
	    command_list_->ResourceBarrier(1, &barrier);

	    if (FAILED(command_list_->Close())) 
		{
	        state_.status_message = "关闭 D3D12 命令列表失败";
	        return false;
	    }

	    ID3D12CommandList* const command_lists[] = {
	        static_cast<ID3D12CommandList*>(command_list_.Get()),
	    };
	    command_queue_->ExecuteCommandLists(static_cast<UINT>(std::size(command_lists)), command_lists);

	    if (!SignalFrameSubmission(*frame_context))
	        return false;

	    const HRESULT present_result = swap_chain_->Present(1, 0);
	    swap_chain_occluded_ = (present_result == DXGI_STATUS_OCCLUDED);

	    if (FAILED(present_result) && present_result != DXGI_STATUS_OCCLUDED) 
		{
	        state_.status_message = "D3D12 交换链呈现失败";
	        return false;
	    }

	    ++frame_index_;
	    return true;
	}

	bool GuiApplication::ResizeSwapChain(const UINT width, const UINT height) 
	{
	    if (swap_chain_ == nullptr || width == 0 || height == 0)
	        return true;

	    WaitForPendingOperations();
	    ResetComArray(render_target_resources_);

	    DXGI_SWAP_CHAIN_DESC1 swap_chain_description{};
	    if (FAILED(swap_chain_->GetDesc1(&swap_chain_description))) 
		{
	        state_.status_message = "读取 D3D12 交换链信息失败";
	        return false;
	    }

	    if (FAILED(swap_chain_->ResizeBuffers(static_cast<UINT>(kBackBufferCount), width, height,
	                                          swap_chain_description.Format,
	                                          swap_chain_description.Flags))) 
		{
	        state_.status_message = "调整 D3D12 交换链大小失败";
	        return false;
	    }

	    return CreateRenderTargets();
	}

	void GuiApplication::MainLoop() 
	{
	    bool keep_running = true;
	    while (keep_running) 
		{
	        MSG message{};
	        while (PeekMessageW(&message, nullptr, 0U, 0U, PM_REMOVE) != 0) 
			{
	            TranslateMessage(&message);
	            DispatchMessageW(&message);
	            if (message.message == WM_QUIT)
	                keep_running = false;
	        }

	        if (!keep_running)
	            break;

	        if (is_minimized_) 
			{
	            Sleep(10);
	            continue;
	        }

	        if ((swap_chain_occluded_ && swap_chain_ != nullptr &&
	             swap_chain_->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) ||
	            IsIconic(window_handle_) != FALSE) 
			{
	            Sleep(10);
	            continue;
	        }
	        swap_chain_occluded_ = false;
            RefreshManagedConnectionState();

	        ImGui_ImplDX12_NewFrame();
	        ImGui_ImplWin32_NewFrame();
	        ImGui::NewFrame();

	        RenderUserInterface();

	        ImGui::Render();
	        if (!RenderFrame())
	            keep_running = false;

            ProcessPendingConnectionAction();
	    }
	}

	void GuiApplication::RenderUserInterface() 
	{
	    ImGuiViewport* viewport = ImGui::GetMainViewport();
	    ImGui::SetNextWindowPos(viewport->WorkPos);
	    ImGui::SetNextWindowSize(viewport->WorkSize);
	    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0F, 8.0F));
	    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0F);
	    ImGui::Begin("RDC Desktop Root",
	                 nullptr,
	                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
	                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);

	    ImDrawList* draw_list = ImGui::GetWindowDrawList();
	    const ImVec2 root_position = ImGui::GetWindowPos();
	    const ImVec2 root_size = ImGui::GetWindowSize();
	    draw_list->AddRectFilled(root_position,
	                             ImVec2(root_position.x + root_size.x, root_position.y + root_size.y),
	                             IM_COL32(244, 247, 250, 255));

	    DrawSidebarPanel();
	    ImGui::SameLine(0.0F, 8.0F);
	    DrawContentPanel();

	    ImGui::End();
	    ImGui::PopStyleVar(2);
	}

	void GuiApplication::DrawSidebarPanel() 
	{
	    constexpr ImVec2 kSidebarSize(56.0F, 0.0F);
	    ImGui::BeginChild("SidebarPanel", kSidebarSize, false, ImGuiWindowFlags_NoScrollbar);

	    const ImVec2 panel_position = ImGui::GetWindowPos();
	    const ImVec2 panel_size = ImGui::GetWindowSize();
	    ImGui::GetWindowDrawList()->AddRectFilled(panel_position,
	                                              ImVec2(panel_position.x + panel_size.x, panel_position.y + panel_size.y),
	                                              IM_COL32(230, 245, 253, 255),
	                                              18.0F);
	    ImGui::GetWindowDrawList()->AddRect(panel_position,
	                                        ImVec2(panel_position.x + panel_size.x, panel_position.y + panel_size.y),
	                                        IM_COL32(113, 187, 227, 255),
	                                        18.0F,
	                                        0,
	                                        1.3F);

	    ImGui::SetCursorPos(ImVec2(10.0F, 32.0F));

	    if (widgets::DrawSidebarButton("home",
                                      &home_icon_,
                                      "主",
                                      state_.current_page == NavigationPage::Home,
                                      ImVec2(36.0F, 36.0F)))
	        state_.current_page = NavigationPage::Home;

	    ImGui::SetCursorPos(ImVec2(10.0F, 80.0F));

	    if (widgets::DrawSidebarButton("settings",
                                      &settings_icon_,
                                      "设",
                                      state_.current_page == NavigationPage::Settings,
                                      ImVec2(36.0F, 36.0F)))
	        state_.current_page = NavigationPage::Settings;

	    ImGui::EndChild();
	}

	void GuiApplication::DrawContentPanel() 
	{
	    ImGui::BeginChild("ContentPanel", ImVec2(0.0F, 0.0F), false, ImGuiWindowFlags_NoScrollbar);

	    const ImVec2 panel_position = ImGui::GetWindowPos();
	    const ImVec2 panel_size = ImGui::GetWindowSize();
	    ImGui::GetWindowDrawList()->AddRectFilled(panel_position,
	                                              ImVec2(panel_position.x + panel_size.x, panel_position.y + panel_size.y),
	                                              IM_COL32(248, 248, 251, 255),
	                                              18.0F);
	    ImGui::GetWindowDrawList()->AddRect(panel_position,
	                                        ImVec2(panel_position.x + panel_size.x, panel_position.y + panel_size.y),
	                                        IM_COL32(113, 187, 227, 255),
	                                        18.0F,
	                                        0,
	                                        1.3F);

        if (animated_page_ != state_.current_page) {
            animated_page_ = state_.current_page;
            content_reveal_progress_ = 0.0F;
        }

        content_reveal_progress_ = animations::AnimateTowards(content_reveal_progress_,
                                                              1.0F,
                                                              ImGui::GetIO().DeltaTime,
                                                              10.0F);
        const float content_alpha = animations::EaseOutCubic(content_reveal_progress_);
        const float content_offset_y = animations::Lerp(18.0F, 0.0F, content_alpha);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + content_offset_y);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, content_alpha);

	    if (state_.current_page == NavigationPage::Home) 
		{
	        DrawHomePage();
	    } 
		else 
		{
	        DrawSettingsPage();
	    }

        ImGui::PopStyleVar();
	    ImGui::EndChild();
}

	void GuiApplication::DrawHomePage() 
	{
	    constexpr float kPanelPaddingX = 34.0F;
	    constexpr float kTopOffset = 34.0F;
	    constexpr float kLabelWidth = 210.0F;
	    constexpr float kInputWidth = 260.0F;
	    constexpr float kButtonWidth = 220.0F;
	    constexpr float kButtonHeight = 60.0F;
        constexpr ImGuiInputTextFlags kReadOnlyInputFlags = ImGuiInputTextFlags_ReadOnly;

        std::size_t selected_scheme = static_cast<std::size_t>(state_.page_scheme);
        std::array<char, 128> signal_address_buffer{};
        if (const std::optional<std::string> signal_url = BuildSignalUrl();
            signal_url.has_value()) {
            WriteBufferText(signal_address_buffer, *signal_url);
        }

	    ImGui::SetCursorPos(ImVec2(kPanelPaddingX, kTopOffset));
	    ImGui::BeginGroup();
	    widgets::DrawLabeledInputRow("host_ip",
                                    "主机 IP：",
                                    state_.host_ip,
                                    kLabelWidth,
                                    kInputWidth,
                                    kReadOnlyInputFlags);
	    ImGui::Dummy(ImVec2(0.0F, 18.0F));
        if (widgets::DrawLabeledComboRow("protocol",
                                         "协    议：",
                                         selected_scheme,
                                         kControllerPageSchemeLabels,
                                         kLabelWidth,
                                         kInputWidth)) {
            state_.page_scheme = static_cast<ControllerPageScheme>(selected_scheme);
        }
	    ImGui::Dummy(ImVec2(0.0F, 18.0F));
	    widgets::DrawLabeledInputRow("controller_user_id", "控制端用户 ID：", state_.controller_user_id, kLabelWidth, kInputWidth);
	    ImGui::Dummy(ImVec2(0.0F, 18.0F));
	    widgets::DrawLabeledInputRow("target_device_id", "目标主机设备 ID：", state_.target_device_id, kLabelWidth, kInputWidth);
	    ImGui::Dummy(ImVec2(0.0F, 18.0F));
	    widgets::DrawLabeledInputRow("signal_address",
                                    "信令地址：",
                                    signal_address_buffer,
                                    kLabelWidth,
                                    kInputWidth,
                                    kReadOnlyInputFlags);
	    ImGui::EndGroup();

	    const ImVec2 content_size = ImGui::GetWindowSize();
        float status_offset_y = 42.0F;
        if (!state_.launched_controller_origin.empty()) {
            ImGui::SetCursorPos(ImVec2(kPanelPaddingX, content_size.y - 84.0F));
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(100, 114, 128, 255));
            ImGui::TextWrapped("%s", state_.launched_controller_origin.c_str());
            ImGui::PopStyleColor();
            status_offset_y = 42.0F;
        }

        const bool is_starting = state_.connection_state == ManagedConnectionState::Starting;
        const bool is_connected = state_.connection_state == ManagedConnectionState::Connected;
        const char* button_label = "开始连接";
        ImU32 button_color = IM_COL32(91, 187, 234, 255);
        ImU32 button_hover_color = IM_COL32(80, 178, 227, 255);
        ImU32 button_active_color = IM_COL32(69, 167, 216, 255);
        if (is_starting) {
            button_label = "正在启动 ....";
            button_color = IM_COL32(91, 187, 234, 255);
            button_hover_color = IM_COL32(91, 187, 234, 255);
            button_active_color = IM_COL32(91, 187, 234, 255);
        } else if (is_connected) {
            button_label = "断开连接";
            button_color = IM_COL32(219, 74, 74, 255);
            button_hover_color = IM_COL32(205, 62, 62, 255);
            button_active_color = IM_COL32(190, 52, 52, 255);
        }

	    ImGui::SetCursorPos(ImVec2(content_size.x - kButtonWidth - 24.0F, content_size.y - kButtonHeight - 16.0F));
	    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 28.0F);
	    ImGui::PushStyleColor(ImGuiCol_Button, button_color);
	    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, button_hover_color);
	    ImGui::PushStyleColor(ImGuiCol_ButtonActive, button_active_color);

	    if (ImGui::Button(button_label, ImVec2(kButtonWidth, kButtonHeight))) {
            if (is_connected) {
                RequestStopConnection();
            } else if (!is_starting) {
                RequestStartConnection();
            }
        }

	    ImGui::PopStyleColor(3);
	    ImGui::PopStyleVar();

	    ImGui::SetCursorPos(ImVec2(kPanelPaddingX, content_size.y - status_offset_y));
	    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(72, 95, 114, 255));
	    ImGui::TextUnformatted(state_.status_message.c_str());
	    ImGui::PopStyleColor();
	}

	void GuiApplication::DrawSettingsPage() 
	{
        constexpr float kPanelPaddingX = 34.0F;
        constexpr float kTopOffset = 34.0F;
        constexpr float kLabelWidth = 92.0F;
        constexpr float kInputWidth = 220.0F;
        constexpr ImGuiInputTextFlags kPortInputFlags = ImGuiInputTextFlags_CharsDecimal;

	    ImGui::SetCursorPos(ImVec2(kPanelPaddingX, kTopOffset));
	    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(38, 48, 60, 255));
	    ImGui::TextUnformatted("设置");
	    ImGui::Dummy(ImVec2(0.0F, 18.0F));
        widgets::DrawLabeledInputRow("signal_port",
                                     "端口：",
                                     state_.signal_port,
                                     kLabelWidth,
                                     kInputWidth,
                                     kPortInputFlags);
	    ImGui::PopStyleColor();
	}

    /**
     * @brief 处理主循环中挂起的连接动作。
     */
    void GuiApplication::ProcessPendingConnectionAction() {
        const PendingConnectionAction action = pending_connection_action_;
        if (action == PendingConnectionAction::None) {
            return;
        }

        pending_connection_action_ = PendingConnectionAction::None;
        if (action == PendingConnectionAction::Start) {
            if (!StartManagedConnection()) {
                state_.connection_state = ManagedConnectionState::Disconnected;
            }

            return;
        }

        StopManagedConnection("后台服务已断开");
    }

    /**
     * @brief 请求在后续帧中启动后台服务并打开控制页。
     */
    void GuiApplication::RequestStartConnection() {
        if (state_.connection_state != ManagedConnectionState::Disconnected) {
            return;
        }

        state_.connection_state = ManagedConnectionState::Starting;
        state_.status_message = "正在启动后台服务...";
        pending_connection_action_ = PendingConnectionAction::Start;
    }

    /**
     * @brief 请求在后续帧中断开并关闭后台服务。
     */
    void GuiApplication::RequestStopConnection() {
        if (state_.connection_state != ManagedConnectionState::Connected) {
            return;
        }

        pending_connection_action_ = PendingConnectionAction::Stop;
    }

    /**
     * @brief 启动后台信令服务、主机端并打开控制页。
     * @return 返回是否成功或条件是否满足。
     */
    bool GuiApplication::StartManagedConnection() {
        if (!ReadSignalPort().has_value()) {
            state_.status_message = "端口必须为 1-65535 的数字";
            return false;
        }

        const std::string signal_url = BuildSignalUrl().value_or(std::string{});
        const std::string controller_user_id = std::string(ReadBufferText(state_.controller_user_id));
        const std::string target_device_id = std::string(ReadBufferText(state_.target_device_id));
        if (signal_url.empty()) {
            state_.status_message = "主机 IPv4 地址不可用，无法启动后台服务";
            return false;
        }

        if (controller_user_id.empty()) {
            state_.status_message = "控制端用户 ID 不能为空";
            return false;
        }

        if (target_device_id.empty()) {
            state_.status_message = "目标主机设备 ID 不能为空";
            return false;
        }

        if (state_.page_scheme == ControllerPageScheme::Https && !HasHttpsEnvironmentConfiguration()) {
            state_.status_message = "HTTPS 启动需要先配置 RDC_SIGNAL_CERT 和 RDC_SIGNAL_KEY 环境变量";
            return false;
        }

        if (state_.page_scheme == ControllerPageScheme::Https) {
            state_.status_message = "当前控制页启动链尚未支持 HTTPS 页面配合固定 ws:// 信令地址";
            return false;
        }

        state_.launched_controller_origin.clear();
        if (!EnsureManagedProcessJob()) {
            return false;
        }

        if (!LaunchServerProcess()) {
            StopManagedConnection({});
            return false;
        }

        if (!LaunchHostProcess()) {
            StopManagedConnection({});
            return false;
        }

        if (!OpenControllerPage()) {
            StopManagedConnection({});
            return false;
        }

        state_.connection_state = ManagedConnectionState::Connected;
        state_.status_message = "后台服务已启动，可点击断开连接";
        return true;
    }

    /**
     * @brief 停止当前由 GUI 托管的后台进程并重置连接展示状态。
     * @param status_message 断开后展示的状态文本。
     */
    void GuiApplication::StopManagedConnection(const std::string_view status_message) {
        CloseManagedProcessGroup();
        pending_connection_action_ = PendingConnectionAction::None;
        state_.connection_state = ManagedConnectionState::Disconnected;
        state_.launched_controller_origin.clear();
        if (!status_message.empty()) {
            state_.status_message = std::string(status_message);
        }
    }

    /**
     * @brief 检查托管进程是否仍在运行，并在异常退出时回收状态。
     */
    void GuiApplication::RefreshManagedConnectionState() {
        if (state_.connection_state != ManagedConnectionState::Connected) {
            return;
        }

        if (!IsChildProcessRunning(server_process_)) {
            StopManagedConnection("信令服务已退出，连接已断开");
            return;
        }

        if (!IsChildProcessRunning(host_process_)) {
            StopManagedConnection("主机端进程已退出，连接已断开");
        }
    }

    /**
     * @brief 启动后台信令服务进程。
     * @return 返回是否成功或条件是否满足。
     */
    bool GuiApplication::LaunchServerProcess() {
        const std::optional<std::wstring> executable_path = GetCurrentExecutablePath();
        const std::optional<std::uint16_t> signal_port = ReadSignalPort();
        if (!executable_path.has_value()) {
            state_.status_message = "无法定位当前程序路径，信令服务启动失败";
            return false;
        }

        if (!signal_port.has_value()) {
            state_.status_message = "信令服务启动参数不完整";
            return false;
        }

        const std::wstring wide_signal_port = ToWide(std::to_string(*signal_port));
        if (wide_signal_port.empty()) {
            state_.status_message = "信令服务启动参数编码失败";
            return false;
        }

        const std::wstring command_line = BuildChildProcessCommandLine(
            *executable_path, L"server", std::array<std::wstring_view, 1>{wide_signal_port});
        if (!LaunchChildProcess(command_line, "信令服务", server_process_)) {
            return false;
        }

        return WaitForChildProcessStartup(server_process_, "信令服务");
    }

    /**
     * @brief 启动后台主机端进程。
     * @return 返回是否成功或条件是否满足。
     */
    bool GuiApplication::LaunchHostProcess() {
        const std::optional<std::wstring> executable_path = GetCurrentExecutablePath();
        const std::optional<std::string> signal_url = BuildSignalUrl();
        if (!executable_path.has_value() || !signal_url.has_value()) {
            state_.status_message = "主机端启动参数不完整";
            return false;
        }

        const std::wstring wide_signal_url = ToWide(*signal_url);
        const std::wstring wide_target_device_id =
            ToWide(ReadBufferText(state_.target_device_id));
        if (wide_signal_url.empty() || wide_target_device_id.empty()) {
            state_.status_message = "主机端启动参数编码失败";
            return false;
        }

        const std::wstring command_line = BuildChildProcessCommandLine(
            *executable_path,
            L"host",
            std::array<std::wstring_view, 2>{wide_signal_url, wide_target_device_id});
        if (!LaunchChildProcess(command_line, "主机端进程", host_process_)) {
            return false;
        }

        return WaitForChildProcessStartup(host_process_, "主机端进程");
    }

    /**
     * @brief 创建一个在关闭时自动杀掉全部成员的后台进程作业组。
     * @return 返回是否成功或条件是否满足。
     */
    bool GuiApplication::EnsureManagedProcessJob() {
        CloseManagedProcessGroup();

        managed_process_job_ = ::CreateJobObjectW(nullptr, nullptr);
        if (managed_process_job_ == nullptr) {
            state_.status_message =
                "创建后台进程作业组失败，错误码=" + std::to_string(::GetLastError());
            return false;
        }

        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit_information{};
        limit_information.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (::SetInformationJobObject(managed_process_job_,
                                      JobObjectExtendedLimitInformation,
                                      &limit_information,
                                      sizeof(limit_information)) == FALSE) {
            state_.status_message =
                "配置后台进程作业组失败，错误码=" + std::to_string(::GetLastError());
            ::CloseHandle(managed_process_job_);
            managed_process_job_ = nullptr;
            return false;
        }

        return true;
    }

    /**
     * @brief 终止整个后台进程作业组，并释放与直接子进程关联的句柄。
     */
    void GuiApplication::CloseManagedProcessGroup() {
        if (managed_process_job_ != nullptr) {
            ::TerminateJobObject(managed_process_job_, 0);

            if (host_process_.IsValid()) {
                ::WaitForSingleObject(host_process_.process_handle, 2000);
            }

            if (server_process_.IsValid()) {
                ::WaitForSingleObject(server_process_.process_handle, 2000);
            }

            ::CloseHandle(managed_process_job_);
            managed_process_job_ = nullptr;
            ReleaseChildProcessHandle(host_process_);
            ReleaseChildProcessHandle(server_process_);
            return;
        }

        CloseChildProcess(host_process_);
        CloseChildProcess(server_process_);
    }

    /**
<<<<<<< HEAD
     * @brief 按当前构建配置启动一个受管子进程。
=======
     * @brief 以隐藏窗口方式启动一个受管子进程。
>>>>>>> ec6c746a58750b061c0e595b5410919ddc2500b1
     * @param command_line 子进程完整命令行。
     * @param process_name 用于状态提示的进程名称。
     * @param out_process 输出受管子进程句柄。
     * @return 返回是否成功或条件是否满足。
     */
    bool GuiApplication::LaunchChildProcess(const std::wstring& command_line,
                                            const std::string_view process_name,
                                            ManagedChildProcess& out_process) {
        STARTUPINFOW startup_info{};
        startup_info.cb = sizeof(startup_info);
        startup_info.dwFlags = STARTF_USESHOWWINDOW;
<<<<<<< HEAD

#ifdef _DEBUG
        startup_info.wShowWindow = SW_SHOWDEFAULT;
#else
        startup_info.wShowWindow = SW_HIDE;
#endif

        PROCESS_INFORMATION process_information{};
        std::wstring mutable_command_line = command_line;
#ifdef _DEBUG
        constexpr DWORD kCreationFlags = CREATE_SUSPENDED;
#else
        constexpr DWORD kCreationFlags = CREATE_NO_WINDOW | CREATE_SUSPENDED;
#endif
=======
        startup_info.wShowWindow = SW_HIDE;

        PROCESS_INFORMATION process_information{};
        std::wstring mutable_command_line = command_line;
        constexpr DWORD kCreationFlags = CREATE_NO_WINDOW | CREATE_SUSPENDED;
>>>>>>> ec6c746a58750b061c0e595b5410919ddc2500b1
        const BOOL created = ::CreateProcessW(nullptr,
                                              mutable_command_line.data(),
                                              nullptr,
                                              nullptr,
                                              FALSE,
                                              kCreationFlags,
                                              nullptr,
                                              nullptr,
                                              &startup_info,
                                              &process_information);
        if (created == FALSE) {
            state_.status_message =
                std::string(process_name) + "启动失败，错误码=" + std::to_string(::GetLastError());
            return false;
        }

        const auto fail_and_cleanup = [&](const std::string& message) {
            ::TerminateProcess(process_information.hProcess, 0);
            ::WaitForSingleObject(process_information.hProcess, 2000);
            ::CloseHandle(process_information.hThread);
            ::CloseHandle(process_information.hProcess);
            state_.status_message = message;
        };

        if (managed_process_job_ == nullptr) {
            fail_and_cleanup(std::string(process_name) + "未绑定后台进程作业组");
            return false;
        }

        if (::AssignProcessToJobObject(managed_process_job_, process_information.hProcess) == FALSE) {
            fail_and_cleanup(std::string(process_name) +
                             "加入后台进程作业组失败，错误码=" +
                             std::to_string(::GetLastError()));
            return false;
        }

        if (::ResumeThread(process_information.hThread) == static_cast<DWORD>(-1)) {
            fail_and_cleanup(std::string(process_name) +
                             "恢复运行失败，错误码=" +
                             std::to_string(::GetLastError()));
            return false;
        }

        ::CloseHandle(process_information.hThread);
        out_process.process_handle = process_information.hProcess;
        out_process.process_id = process_information.dwProcessId;
        return true;
    }

    /**
     * @brief 等待刚启动的子进程进入稳定运行状态。
     * @param process 待检查的受管进程。
     * @param process_name 用于状态提示的进程名称。
     * @return 返回是否成功或条件是否满足。
     */
    bool GuiApplication::WaitForChildProcessStartup(const ManagedChildProcess& process,
                                                    const std::string_view process_name) {
        if (!process.IsValid()) {
            state_.status_message = std::string(process_name) + "句柄无效";
            return false;
        }

        for (int attempt = 0; attempt < kChildProcessStartupPollCount; ++attempt) {
            const DWORD wait_result = ::WaitForSingleObject(process.process_handle, 0);
            if (wait_result == WAIT_TIMEOUT) {
                std::this_thread::sleep_for(kChildProcessStartupPollInterval);
                continue;
            }

            DWORD exit_code = 0;
            ::GetExitCodeProcess(process.process_handle, &exit_code);
            state_.status_message =
                std::string(process_name) + "启动失败，退出码=" + std::to_string(exit_code);
            return false;
        }

        return true;
    }

    /**
     * @brief 检查指定子进程当前是否仍在运行。
     * @param process 待检查的受管进程。
     * @return 返回是否成功或条件是否满足。
     */
    bool GuiApplication::IsChildProcessRunning(const ManagedChildProcess& process) const {
        if (!process.IsValid()) {
            return false;
        }

        return ::WaitForSingleObject(process.process_handle, 0) == WAIT_TIMEOUT;
    }

    /**
     * @brief 终止并关闭一个受管子进程句柄。
     * @param process 待停止的受管进程。
     */
    void GuiApplication::CloseChildProcess(ManagedChildProcess& process) {
        if (!process.IsValid()) {
            return;
        }

        if (::WaitForSingleObject(process.process_handle, 0) == WAIT_TIMEOUT) {
            ::TerminateProcess(process.process_handle, 0);
            ::WaitForSingleObject(process.process_handle, 2000);
        }

        ::CloseHandle(process.process_handle);
        process = {};
    }

    void GuiApplication::ReleaseChildProcessHandle(ManagedChildProcess& process) {
        if (!process.IsValid()) {
            return;
        }

        ::CloseHandle(process.process_handle);
        process = {};
    }

    std::optional<std::uint16_t> GuiApplication::ReadSignalPort() const {
        return ParsePortText(ReadBufferText(state_.signal_port));
    }

	std::optional<std::string> GuiApplication::BuildSignalUrl() const
	{
	    const std::string host_ip = std::string(ReadBufferText(state_.host_ip));
        const std::optional<std::uint16_t> signal_port = ReadSignalPort();
	    if (host_ip.empty() || !signal_port.has_value())
	        return std::nullopt;

	    return std::string("ws://") + host_ip + ":" + std::to_string(*signal_port) + "/signal";
	}

	std::optional<std::string> GuiApplication::BuildControllerOrigin() const
	{
        const std::string host_ip = std::string(ReadBufferText(state_.host_ip));
        const std::optional<std::uint16_t> signal_port = ReadSignalPort();
        if (host_ip.empty() || !signal_port.has_value()) {
	        return std::nullopt;
        }

        return std::string(GetControllerPageSchemeText(state_.page_scheme)) + "://" + host_ip +
               ":" + std::to_string(*signal_port);
	}

	std::optional<std::string> GuiApplication::BuildControllerPageUrl() const
	{
	    const std::optional<std::string> signal_url = BuildSignalUrl();
        const std::optional<std::string> controller_origin = BuildControllerOrigin();
	    if (!signal_url.has_value() || !controller_origin.has_value())
	        return std::nullopt;

	    std::string page_url = *controller_origin;
        page_url += "/?signal=" + UrlEncode(*signal_url) +
	                "&user=" + UrlEncode(ReadBufferText(state_.controller_user_id)) +
	                "&target=" + UrlEncode(ReadBufferText(state_.target_device_id));
	    return page_url;
	}

	bool GuiApplication::OpenControllerPage() 
	{
	    const std::optional<std::string> page_url = BuildControllerPageUrl();
        const std::optional<std::string> controller_origin = BuildControllerOrigin();
	    if (!page_url.has_value()) 
		{
	        state_.status_message = "主机 IPv4 地址不可用，无法生成控制页地址";
	        return false;
	    }

	    const std::wstring wide_url = ToWide(*page_url);
	    if (wide_url.empty()) 
		{
	        state_.status_message = "控制页地址编码失败";
	        return false;
	    }

	    const auto result = reinterpret_cast<std::intptr_t>(
	        ShellExecuteW(window_handle_, L"open", wide_url.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
	    if (result <= 32) 
		{
	        state_.status_message = "打开浏览器失败，请确认系统默认浏览器是否可用";
	        return false;
	    }

        if (controller_origin.has_value()) {
            state_.launched_controller_origin = *controller_origin;
        }

	    state_.status_message = "已在默认浏览器中打开控制页";
	    return true;
	}

	LRESULT GuiApplication::HandleWindowMessage(const UINT message, const WPARAM w_param, const LPARAM l_param) 
	{
	    if (win32_backend_initialized_ &&::ImGui_ImplWin32_WndProcHandler(window_handle_, message, w_param, l_param) != 0)
	        return 1;

	    switch (message)
		{
		    case WM_SIZE:
		        if (w_param == SIZE_MINIMIZED) 
				{
		            is_minimized_ = true;
		            return 0;
		        }

		        if (device_ != nullptr) 
				{
		            is_minimized_ = false;
		            const UINT width = static_cast<UINT>(LOWORD(l_param));
		            const UINT height = static_cast<UINT>(HIWORD(l_param));
		            ResizeSwapChain(width, height);
		        }
		        return 0;
		    case WM_SYSCOMMAND:
		        if ((w_param & 0xFFF0U) == SC_KEYMENU)
		            return 0;
		        break;
		    case WM_DESTROY:
		        PostQuitMessage(0);
		        return 0;
		    default:
		        break;
	    }

	    return DefWindowProcW(window_handle_, message, w_param, l_param);
	}

	void GuiApplication::AllocateSrvDescriptor(ImGui_ImplDX12_InitInfo* init_info,
	                                           D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu_desc_handle,
	                                           D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_desc_handle) 
	{
	    if (init_info == nullptr || init_info->UserData == nullptr)
	        return;

	    auto* self = static_cast<GuiApplication*>(init_info->UserData);

	    if (!self->srv_descriptor_allocator_.Allocate(out_cpu_desc_handle, out_gpu_desc_handle))
		{
	        if (out_cpu_desc_handle != nullptr) 
	            *out_cpu_desc_handle = {};

	        if (out_gpu_desc_handle != nullptr)
	            *out_gpu_desc_handle = {};
	    }
	}

	void GuiApplication::FreeSrvDescriptor(ImGui_ImplDX12_InitInfo* init_info,
	                                       const D3D12_CPU_DESCRIPTOR_HANDLE cpu_desc_handle,
	                                       const D3D12_GPU_DESCRIPTOR_HANDLE gpu_desc_handle) 
	{
	    if (init_info == nullptr || init_info->UserData == nullptr)
	        return;

	    auto* self = static_cast<GuiApplication*>(init_info->UserData);
	    self->srv_descriptor_allocator_.Free(cpu_desc_handle, gpu_desc_handle);
	}

	LRESULT CALLBACK GuiApplication::WindowProc(HWND window_handle,
	                                            const UINT message,
	                                            const WPARAM w_param,
	                                            const LPARAM l_param) 
	{
	    if (message == WM_NCCREATE) 
		{
	        const auto* create_struct = reinterpret_cast<const CREATESTRUCTW*>(l_param);
	        auto* self = static_cast<GuiApplication*>(create_struct->lpCreateParams);
	        SetWindowLongPtrW(window_handle, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
	        self->window_handle_ = window_handle;
	    }

	    auto* self = reinterpret_cast<GuiApplication*>(GetWindowLongPtrW(window_handle, GWLP_USERDATA));
	    if (self != nullptr)
	        return self->HandleWindowMessage(message, w_param, l_param);

	    return DefWindowProcW(window_handle, message, w_param, l_param);
	}

}  // namespace rdc::ui::detail

namespace rdc::ui 
{

	int RunMain() 
	{
	    detail::GuiApplication application;
	    return application.Run();
	}

}  // namespace rdc::ui
