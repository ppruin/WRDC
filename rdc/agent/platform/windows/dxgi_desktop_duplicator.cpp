/**
 * @file dxgi_desktop_duplicator.cpp
 * @brief 实现 agent/platform/windows/dxgi_desktop_duplicator 相关的类型、函数与流程。
 */

#include "dxgi_desktop_duplicator.hpp"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>

#include "../../../protocol/common/buffer_utils.hpp"

namespace rdc::agent::platform::windows {

namespace {

/**
 * @brief 封装 DXGI 帧资源的自动释放逻辑。
 */
class ScopedFrameRelease {
public:
    /**
     * @brief 执行 Scoped帧Release 相关处理。
     * @param duplication duplication。
     */
    explicit ScopedFrameRelease(IDXGIOutputDuplication* duplication)
        : duplication_(duplication) {
    }

    /**
     * @brief 析构 ScopedFrameRelease 对象并释放相关资源。
     */
    ~ScopedFrameRelease() {
        if (duplication_ != nullptr) {
            duplication_->ReleaseFrame();
        }
    }

    /**
     * @brief 构造 ScopedFrameRelease 对象。
     */
    ScopedFrameRelease(const ScopedFrameRelease&) = delete;
    ScopedFrameRelease& operator=(const ScopedFrameRelease&) = delete;

private:
    IDXGIOutputDuplication* duplication_;
};

[[nodiscard]] std::runtime_error MakeError(const std::string& message, const HRESULT hr) {
    return std::runtime_error(message + " (hr=0x" + std::to_string(static_cast<unsigned long>(hr)) + ")");
/**
 * @brief 调整AndTransform大小。
 * @param destination destination。
 * @param source source。
 * @param count count。
 * @param converter converter。
 */
}

template <typename TDestination, typename TSource, typename Converter>
void ResizeAndTransform(std::vector<TDestination>& destination,
                        const TSource* source,
                        const std::size_t count,
                        Converter&& converter) {
    destination.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        destination[i] = converter(source[i]);
    }
}

/**
 * @brief 将输入值转换为脏区矩形表示。
 * @param rect rect。
 * @return 返回对应结果。
 */
capture::DesktopDirtyRect ToDirtyRect(const RECT& rect) {
    return capture::DesktopDirtyRect{
        .left = rect.left,
        .top = rect.top,
        .right = rect.right,
        .bottom = rect.bottom,
    };
}

/**
 * @brief 将输入值转换为移动矩形表示。
 * @param rect rect。
 * @return 返回对应结果。
 */
capture::DesktopMoveRect ToMoveRect(const DXGI_OUTDUPL_MOVE_RECT& rect) {
    return capture::DesktopMoveRect{
        .source_x = rect.SourcePoint.x,
        .source_y = rect.SourcePoint.y,
        .destination_left = rect.DestinationRect.left,
        .destination_top = rect.DestinationRect.top,
        .destination_right = rect.DestinationRect.right,
        .destination_bottom = rect.DestinationRect.bottom,
    };
}

/**
 * @brief 构造 DxgiDesktopDuplicator 对象。
 * @param output_index 显示输出索引。
 */
}  // namespace

DxgiDesktopDuplicator::DxgiDesktopDuplicator(const std::uint32_t output_index)
    : output_index_(output_index) {
    InitializeDevice();
    InitializeDuplication();
}

/**
 * @brief 采集Next帧。
 * @param timeout 等待超时时间。
 * @return 返回可用结果；失败时返回空值。
 */
std::optional<capture::DesktopFrame> DxgiDesktopDuplicator::CaptureNextFrame(const std::chrono::milliseconds timeout) {
    DXGI_OUTDUPL_FRAME_INFO frame_info{};
    Microsoft::WRL::ComPtr<IDXGIResource> desktop_resource;
    const auto timeout_ms = static_cast<UINT>(std::max<std::int64_t>(0, timeout.count()));
    const HRESULT hr = duplication_->AcquireNextFrame(timeout_ms, &frame_info, &desktop_resource);

    if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
        return std::nullopt;
    }

    if (hr == DXGI_ERROR_ACCESS_LOST) {
        ReinitializeDuplication();
        return std::nullopt;
    }

    if (FAILED(hr)) {
        throw MakeError("AcquireNextFrame 调用失败", hr);
    }

    ScopedFrameRelease frame_release(duplication_.Get());

    Microsoft::WRL::ComPtr<ID3D11Texture2D> source_texture;
    const HRESULT resource_hr = desktop_resource.As(&source_texture);
    if (FAILED(resource_hr) || source_texture == nullptr) {
        throw MakeError("查询捕获到的桌面纹理失败", resource_hr);
    }

    return CopyFrame(source_texture.Get(), frame_info);
}

/**
 * @brief 初始化设备。
 */
void DxgiDesktopDuplicator::InitializeDevice() {
    UINT creation_flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if defined(_DEBUG)
    creation_flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    constexpr D3D_FEATURE_LEVEL kFeatureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };

    D3D_FEATURE_LEVEL created_feature_level{};
    const HRESULT hr = D3D11CreateDevice(nullptr,
                                         D3D_DRIVER_TYPE_HARDWARE,
                                         nullptr,
                                         creation_flags,
                                         kFeatureLevels,
                                         static_cast<UINT>(sizeof(kFeatureLevels) / sizeof(kFeatureLevels[0])),
                                         D3D11_SDK_VERSION,
                                         &device_,
                                         &created_feature_level,
                                         &device_context_);

    if (hr == DXGI_ERROR_SDK_COMPONENT_MISSING && (creation_flags & D3D11_CREATE_DEVICE_DEBUG) != 0) {
        creation_flags &= ~D3D11_CREATE_DEVICE_DEBUG;
        const HRESULT fallback_hr = D3D11CreateDevice(nullptr,
                                                      D3D_DRIVER_TYPE_HARDWARE,
                                                      nullptr,
                                                      creation_flags,
                                                      kFeatureLevels,
                                                      static_cast<UINT>(sizeof(kFeatureLevels) / sizeof(kFeatureLevels[0])),
                                                      D3D11_SDK_VERSION,
                                                      &device_,
                                                      &created_feature_level,
                                                      &device_context_);
        if (FAILED(fallback_hr)) {
            throw MakeError("D3D11CreateDevice 调用失败", fallback_hr);
        }

        return;
    }

    if (FAILED(hr)) {
        throw MakeError("D3D11CreateDevice 调用失败", hr);
    }

    (void)created_feature_level;
}

/**
 * @brief 初始化Duplication。
 */
void DxgiDesktopDuplicator::InitializeDuplication() {
    output_ = ResolveOutput(output_index_);
    if (output_ == nullptr) {
        throw std::runtime_error("未找到请求输出索引对应的桌面输出设备");
    }

    duplication_.Reset();
    const HRESULT hr = output_->DuplicateOutput(device_.Get(), &duplication_);
    if (FAILED(hr)) {
        throw MakeError("IDXGIOutput1::DuplicateOutput 调用失败", hr);
    }

    duplication_->GetDesc(&duplication_desc_);
    frame_texture_.Reset();
    frame_texture_desc_ = {};
    move_rect_buffer_.clear();
    dirty_rect_buffer_.clear();
}

/**
 * @brief 执行 `ReinitializeDuplication` 对应的处理逻辑。
 */
void DxgiDesktopDuplicator::ReinitializeDuplication() {
    duplication_.Reset();
    InitializeDuplication();
}

/**
 * @brief 确保帧Texture已就绪。
 * @param source_texture sourcetexture。
 */
void DxgiDesktopDuplicator::EnsureFrameTexture(ID3D11Texture2D* source_texture) {
    D3D11_TEXTURE2D_DESC source_desc{};
    source_texture->GetDesc(&source_desc);

    if (frame_texture_ != nullptr &&
        frame_texture_desc_.Width == source_desc.Width &&
        frame_texture_desc_.Height == source_desc.Height &&
        frame_texture_desc_.Format == source_desc.Format &&
        frame_texture_desc_.ArraySize == source_desc.ArraySize &&
        frame_texture_desc_.SampleDesc.Count == source_desc.SampleDesc.Count &&
        frame_texture_desc_.SampleDesc.Quality == source_desc.SampleDesc.Quality &&
        frame_texture_desc_.MipLevels == source_desc.MipLevels) {
        return;
    }

    D3D11_TEXTURE2D_DESC copy_desc = source_desc;
    copy_desc.BindFlags = 0;
    copy_desc.MiscFlags = 0;
    copy_desc.CPUAccessFlags = 0;
    copy_desc.Usage = D3D11_USAGE_DEFAULT;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> frame_texture;
    const HRESULT texture_hr = device_->CreateTexture2D(&copy_desc, nullptr, &frame_texture);
    if (FAILED(texture_hr) || frame_texture == nullptr) {
        throw MakeError("创建桌面帧纹理失败", texture_hr);
    }

    frame_texture_ = std::move(frame_texture);
    frame_texture_desc_ = copy_desc;
}

/**
 * @brief 定位输出。
 * @param output_index 显示输出索引。
 * @return 返回对象指针或句柄。
 */
Microsoft::WRL::ComPtr<IDXGIOutput1> DxgiDesktopDuplicator::ResolveOutput(const std::uint32_t output_index) const {
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    const HRESULT factory_hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    if (FAILED(factory_hr)) {
        throw MakeError("CreateDXGIFactory1 调用失败", factory_hr);
    }

    std::uint32_t attached_output_count = 0;
    for (UINT adapter_index = 0;; ++adapter_index) {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        const HRESULT adapter_hr = factory->EnumAdapters1(adapter_index, &adapter);
        if (adapter_hr == DXGI_ERROR_NOT_FOUND) {
            break;
        }

        if (FAILED(adapter_hr)) {
            throw MakeError("IDXGIFactory1::EnumAdapters1 调用失败", adapter_hr);
        }

        for (UINT local_output_index = 0;; ++local_output_index) {
            Microsoft::WRL::ComPtr<IDXGIOutput> output;
            const HRESULT output_hr = adapter->EnumOutputs(local_output_index, &output);
            if (output_hr == DXGI_ERROR_NOT_FOUND) {
                break;
            }

            if (FAILED(output_hr)) {
                throw MakeError("IDXGIAdapter1::EnumOutputs 调用失败", output_hr);
            }

            DXGI_OUTPUT_DESC output_desc{};
            const HRESULT desc_hr = output->GetDesc(&output_desc);
            if (FAILED(desc_hr)) {
                throw MakeError("IDXGIOutput::GetDesc 调用失败", desc_hr);
            }

            if (!output_desc.AttachedToDesktop) {
                continue;
            }

            if (attached_output_count == output_index) {
                Microsoft::WRL::ComPtr<IDXGIOutput1> output1;
                const HRESULT as_hr = output.As(&output1);
                if (FAILED(as_hr) || output1 == nullptr) {
                    throw MakeError("查询 IDXGIOutput1 接口失败", as_hr);
                }

                return output1;
            }

            ++attached_output_count;
        }
    }

    return nullptr;
}

/**
 * @brief 复制帧。
 * @param source_texture sourcetexture。
 * @param frame_info frameinfo。
 * @return 返回对应结果。
 */
capture::DesktopFrame DxgiDesktopDuplicator::CopyFrame(ID3D11Texture2D* source_texture,
                                                       const DXGI_OUTDUPL_FRAME_INFO& frame_info) {
    EnsureFrameTexture(source_texture);
    device_context_->CopyResource(frame_texture_.Get(), source_texture);

    capture::DesktopFrame frame;
    frame.width = duplication_desc_.ModeDesc.Width;
    frame.height = duplication_desc_.ModeDesc.Height;
    frame.pixel_format = capture::DesktopFramePixelFormat::Bgra8Unorm;
    frame.present_qpc_ticks = frame_info.LastPresentTime.QuadPart;
    frame.accumulated_frames = frame_info.AccumulatedFrames;
    frame.texture = frame_texture_;

    if (frame_info.TotalMetadataBufferSize == 0) {
        return frame;
    }

    const auto move_capacity = static_cast<std::size_t>((frame_info.TotalMetadataBufferSize +
                                                         sizeof(DXGI_OUTDUPL_MOVE_RECT) - 1) /
                                                        sizeof(DXGI_OUTDUPL_MOVE_RECT));
    protocol::common::ResizeIfSmaller(move_rect_buffer_, move_capacity);

    UINT move_rect_bytes = 0;
    const HRESULT move_hr = duplication_->GetFrameMoveRects(frame_info.TotalMetadataBufferSize,
                                                            move_rect_buffer_.data(),
                                                            &move_rect_bytes);
    if (FAILED(move_hr)) {
        throw MakeError("GetFrameMoveRects 调用失败", move_hr);
    }

    const auto move_rect_count = static_cast<std::size_t>(move_rect_bytes / sizeof(DXGI_OUTDUPL_MOVE_RECT));
    ResizeAndTransform(frame.move_rects, move_rect_buffer_.data(), move_rect_count, ToMoveRect);

    UINT dirty_rect_bytes = 0;
    if (move_rect_bytes < frame_info.TotalMetadataBufferSize) {
        const auto remaining_metadata_bytes = frame_info.TotalMetadataBufferSize - move_rect_bytes;
        const auto dirty_capacity = static_cast<std::size_t>((remaining_metadata_bytes + sizeof(RECT) - 1) / sizeof(RECT));
        protocol::common::ResizeIfSmaller(dirty_rect_buffer_, dirty_capacity);

        const HRESULT dirty_hr = duplication_->GetFrameDirtyRects(remaining_metadata_bytes,
                                                                  dirty_rect_buffer_.data(),
                                                                  &dirty_rect_bytes);
        if (FAILED(dirty_hr)) {
            throw MakeError("GetFrameDirtyRects 调用失败", dirty_hr);
        }

        const auto dirty_rect_count = static_cast<std::size_t>(dirty_rect_bytes / sizeof(RECT));
        ResizeAndTransform(frame.dirty_rects, dirty_rect_buffer_.data(), dirty_rect_count, ToDirtyRect);
    }

    return frame;
}

}  // namespace rdc::agent::platform::windows
