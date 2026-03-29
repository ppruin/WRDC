/**
 * @file d3d11_desktop_frame_reader.cpp
 * @brief 实现 agent/platform/windows/d3d11_desktop_frame_reader 相关的类型、函数与流程。
 */

#include "d3d11_desktop_frame_reader.hpp"

#include <stdexcept>
#include <string>

#include "../../../protocol/common/buffer_utils.hpp"

namespace rdc::agent::platform::windows {

namespace {

[[nodiscard]] std::runtime_error MakeError(const std::string& message, const HRESULT hr) {
    return std::runtime_error(message + " (hr=0x" + std::to_string(static_cast<unsigned long>(hr)) + ")");
/**
 * @brief 读取相关流程。
 * @param frame 视频帧对象。
 */
}

}  // namespace

encoder::RawVideoFrame D3D11DesktopFrameReader::Read(const capture::DesktopFrame& frame) {
    if (frame.texture == nullptr) {
        throw std::runtime_error("桌面帧缺少纹理对象");
    }

    EnsureContext(frame.texture.Get());
    EnsureStagingTexture(frame.texture.Get());

    device_context_->CopyResource(staging_texture_.Get(), frame.texture.Get());

    D3D11_MAPPED_SUBRESOURCE mapped{};
    const HRESULT map_hr = device_context_->Map(staging_texture_.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(map_hr)) {
        throw MakeError("ID3D11DeviceContext::Map 调用失败", map_hr);
    }

    encoder::RawVideoFrame raw_frame;
    raw_frame.width = frame.width;
    raw_frame.height = frame.height;
    raw_frame.pixel_format = encoder::RawVideoPixelFormat::Bgra8Unorm;
    raw_frame.present_qpc_ticks = frame.present_qpc_ticks;
    raw_frame.stride_bytes = frame.width * 4;
    raw_frame.bytes.resize(static_cast<std::size_t>(raw_frame.stride_bytes) * raw_frame.height);

    const auto* source = static_cast<const std::uint8_t*>(mapped.pData);
    auto* destination = raw_frame.bytes.data();
    protocol::common::CopyRows<4>(
        source,
        mapped.RowPitch,
        destination,
        raw_frame.stride_bytes,
        raw_frame.width,
        raw_frame.height);

    device_context_->Unmap(staging_texture_.Get(), 0);
    return raw_frame;
}

/**
 * @brief 确保Context已就绪。
 * @param source_texture sourcetexture。
 */
void D3D11DesktopFrameReader::EnsureContext(ID3D11Texture2D* source_texture) {
    Microsoft::WRL::ComPtr<ID3D11Device> source_device;
    source_texture->GetDevice(&source_device);
    if (source_device == nullptr) {
        throw std::runtime_error("桌面纹理未暴露 D3D11 设备");
    }

    if (device_.Get() == source_device.Get() && device_context_ != nullptr) {
        return;
    }

    device_ = std::move(source_device);
    device_context_.Reset();
    device_->GetImmediateContext(&device_context_);
    if (device_context_ == nullptr) {
        throw std::runtime_error("获取 D3D11 立即上下文失败");
    }

    staging_texture_.Reset();
    staging_desc_ = {};
}

/**
 * @brief 确保StagingTexture已就绪。
 * @param source_texture sourcetexture。
 */
void D3D11DesktopFrameReader::EnsureStagingTexture(ID3D11Texture2D* source_texture) {
    D3D11_TEXTURE2D_DESC source_desc{};
    source_texture->GetDesc(&source_desc);

    if (source_desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM &&
        source_desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) {
        throw std::runtime_error("当前桌面纹理格式不支持 BGRA 回读");
    }

    if (staging_texture_ != nullptr &&
        staging_desc_.Width == source_desc.Width &&
        staging_desc_.Height == source_desc.Height &&
        staging_desc_.Format == source_desc.Format &&
        staging_desc_.ArraySize == source_desc.ArraySize &&
        staging_desc_.SampleDesc.Count == source_desc.SampleDesc.Count &&
        staging_desc_.MipLevels == source_desc.MipLevels) {
        return;
    }

    D3D11_TEXTURE2D_DESC staging_desc = source_desc;
    staging_desc.BindFlags = 0;
    staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    staging_desc.Usage = D3D11_USAGE_STAGING;
    staging_desc.MiscFlags = 0;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging_texture;
    const HRESULT create_hr = device_->CreateTexture2D(&staging_desc, nullptr, &staging_texture);
    if (FAILED(create_hr) || staging_texture == nullptr) {
        throw MakeError("创建 D3D11 staging 纹理失败", create_hr);
    }

    staging_texture_ = std::move(staging_texture);
    staging_desc_ = staging_desc;
}

}  // namespace rdc::agent::platform::windows
