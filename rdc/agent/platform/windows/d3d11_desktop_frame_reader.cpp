/**
 * @file d3d11_desktop_frame_reader.cpp
 * @brief 实现 agent/platform/windows/d3d11_desktop_frame_reader 相关的类型、函数与流程。
 */

#include "d3d11_desktop_frame_reader.hpp"

#include <algorithm>
#include <cstring>
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

struct ScopedGdiObject {
    HGDIOBJ handle = nullptr;

    explicit ScopedGdiObject(HGDIOBJ object_handle)
        : handle(object_handle) {
    }

    ~ScopedGdiObject() {
        if (handle != nullptr) {
            DeleteObject(handle);
        }
    }

    ScopedGdiObject(const ScopedGdiObject&) = delete;
    ScopedGdiObject& operator=(const ScopedGdiObject&) = delete;
};

[[nodiscard]] bool LooksLikeStraightAlpha(const std::uint8_t blue,
                                          const std::uint8_t green,
                                          const std::uint8_t red,
                                          const std::uint8_t alpha) {
    return blue > alpha || green > alpha || red > alpha;
}

}  // namespace

D3D11DesktopFrameReader::~D3D11DesktopFrameReader() {
    if (cursor_surface_dc_ != nullptr) {
        if (cursor_surface_old_bitmap_ != nullptr) {
            SelectObject(cursor_surface_dc_, cursor_surface_old_bitmap_);
            cursor_surface_old_bitmap_ = nullptr;
        }

        DeleteDC(cursor_surface_dc_);
        cursor_surface_dc_ = nullptr;
    }

    if (cursor_surface_bitmap_ != nullptr) {
        DeleteObject(cursor_surface_bitmap_);
        cursor_surface_bitmap_ = nullptr;
    }

    cursor_surface_bits_ = nullptr;
}

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
    OverlayVisibleCursor(frame, raw_frame);
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

void D3D11DesktopFrameReader::EnsureCursorSurface(const std::int32_t width, const std::int32_t height) {
    if (width <= 0 || height <= 0) {
        throw std::runtime_error("鼠标叠加 Surface 尺寸非法");
    }

    if (cursor_surface_dc_ == nullptr) {
        cursor_surface_dc_ = CreateCompatibleDC(nullptr);
        if (cursor_surface_dc_ == nullptr) {
            throw std::runtime_error("创建鼠标叠加内存 DC 失败");
        }
    }

    if (cursor_surface_bitmap_ != nullptr &&
        cursor_surface_width_ == width &&
        cursor_surface_height_ == height &&
        cursor_surface_bits_ != nullptr) {
        return;
    }

    if (cursor_surface_bitmap_ != nullptr) {
        if (cursor_surface_old_bitmap_ != nullptr) {
            SelectObject(cursor_surface_dc_, cursor_surface_old_bitmap_);
            cursor_surface_old_bitmap_ = nullptr;
        }

        DeleteObject(cursor_surface_bitmap_);
        cursor_surface_bitmap_ = nullptr;
        cursor_surface_bits_ = nullptr;
    }

    BITMAPINFO bitmap_info{};
    bitmap_info.bmiHeader.biSize = sizeof(bitmap_info.bmiHeader);
    bitmap_info.bmiHeader.biWidth = width;
    bitmap_info.bmiHeader.biHeight = -height;
    bitmap_info.bmiHeader.biPlanes = 1;
    bitmap_info.bmiHeader.biBitCount = 32;
    bitmap_info.bmiHeader.biCompression = BI_RGB;

    void* dib_bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(cursor_surface_dc_, &bitmap_info, DIB_RGB_COLORS, &dib_bits, nullptr, 0);
    if (bitmap == nullptr || dib_bits == nullptr) {
        throw std::runtime_error("创建鼠标叠加 DIBSection 失败");
    }

    cursor_surface_bitmap_ = bitmap;
    cursor_surface_old_bitmap_ = SelectObject(cursor_surface_dc_, cursor_surface_bitmap_);
    cursor_surface_bits_ = dib_bits;
    cursor_surface_width_ = width;
    cursor_surface_height_ = height;
}

void D3D11DesktopFrameReader::OverlayVisibleCursor(const capture::DesktopFrame& frame,
                                                   encoder::RawVideoFrame& raw_frame) {
    if (raw_frame.bytes.empty() || raw_frame.stride_bytes < raw_frame.width * 4) {
        return;
    }

    CURSORINFO cursor_info{};
    cursor_info.cbSize = sizeof(cursor_info);
    if (GetCursorInfo(&cursor_info) == FALSE ||
        (cursor_info.flags & CURSOR_SHOWING) == 0 ||
        cursor_info.hCursor == nullptr) {
        return;
    }

    ICONINFO icon_info{};
    if (GetIconInfo(cursor_info.hCursor, &icon_info) == FALSE) {
        return;
    }

    ScopedGdiObject color_bitmap{icon_info.hbmColor};
    ScopedGdiObject mask_bitmap{icon_info.hbmMask};

    BITMAP bitmap_desc{};
    HBITMAP measurement_bitmap = icon_info.hbmColor != nullptr ? icon_info.hbmColor : icon_info.hbmMask;
    if (measurement_bitmap == nullptr ||
        GetObject(measurement_bitmap, sizeof(bitmap_desc), &bitmap_desc) == 0) {
        return;
    }

    const std::int32_t cursor_width = bitmap_desc.bmWidth;
    const std::int32_t cursor_height = icon_info.hbmColor != nullptr
        ? bitmap_desc.bmHeight
        : bitmap_desc.bmHeight / 2;
    if (cursor_width <= 0 || cursor_height <= 0) {
        return;
    }

    EnsureCursorSurface(cursor_width, cursor_height);
    std::memset(cursor_surface_bits_, 0, static_cast<std::size_t>(cursor_width) * cursor_height * 4);

    if (DrawIconEx(cursor_surface_dc_,
                   0,
                   0,
                   cursor_info.hCursor,
                   cursor_width,
                   cursor_height,
                   0,
                   nullptr,
                   DI_NORMAL) == FALSE) {
        return;
    }

    const std::int32_t cursor_left =
        cursor_info.ptScreenPos.x - static_cast<std::int32_t>(icon_info.xHotspot) - frame.desktop_left;
    const std::int32_t cursor_top =
        cursor_info.ptScreenPos.y - static_cast<std::int32_t>(icon_info.yHotspot) - frame.desktop_top;

    const std::int32_t clip_left = std::max<std::int32_t>(0, cursor_left);
    const std::int32_t clip_top = std::max<std::int32_t>(0, cursor_top);
    const std::int32_t clip_right = std::min<std::int32_t>(static_cast<std::int32_t>(raw_frame.width), cursor_left + cursor_width);
    const std::int32_t clip_bottom = std::min<std::int32_t>(static_cast<std::int32_t>(raw_frame.height), cursor_top + cursor_height);
    if (clip_left >= clip_right || clip_top >= clip_bottom) {
        return;
    }

    const auto* cursor_pixels = static_cast<const std::uint8_t*>(cursor_surface_bits_);
    const auto cursor_stride = static_cast<std::size_t>(cursor_width) * 4;

    for (std::int32_t y = clip_top; y < clip_bottom; ++y) {
        const auto source_y = static_cast<std::size_t>(y - cursor_top);
        const auto destination_y = static_cast<std::size_t>(y) * raw_frame.stride_bytes;

        for (std::int32_t x = clip_left; x < clip_right; ++x) {
            const auto source_x = static_cast<std::size_t>(x - cursor_left);
            const auto source_offset = source_y * cursor_stride + source_x * 4;
            const auto destination_offset = destination_y + static_cast<std::size_t>(x) * 4;

            const std::uint8_t blue = cursor_pixels[source_offset + 0];
            const std::uint8_t green = cursor_pixels[source_offset + 1];
            const std::uint8_t red = cursor_pixels[source_offset + 2];
            const std::uint8_t alpha = cursor_pixels[source_offset + 3];
            if (alpha == 0) {
                continue;
            }

            auto* destination = raw_frame.bytes.data() + destination_offset;
            if (alpha == 255) {
                destination[0] = blue;
                destination[1] = green;
                destination[2] = red;
                destination[3] = 255;
                continue;
            }

            if (LooksLikeStraightAlpha(blue, green, red, alpha)) {
                destination[0] = static_cast<std::uint8_t>(
                    (static_cast<unsigned>(blue) * alpha +
                     static_cast<unsigned>(destination[0]) * (255 - alpha) + 127) / 255);
                destination[1] = static_cast<std::uint8_t>(
                    (static_cast<unsigned>(green) * alpha +
                     static_cast<unsigned>(destination[1]) * (255 - alpha) + 127) / 255);
                destination[2] = static_cast<std::uint8_t>(
                    (static_cast<unsigned>(red) * alpha +
                     static_cast<unsigned>(destination[2]) * (255 - alpha) + 127) / 255);
            } else {
                destination[0] = static_cast<std::uint8_t>(std::min(
                    255U,
                    static_cast<unsigned>(blue) +
                        (static_cast<unsigned>(destination[0]) * (255 - alpha) + 127) / 255));
                destination[1] = static_cast<std::uint8_t>(std::min(
                    255U,
                    static_cast<unsigned>(green) +
                        (static_cast<unsigned>(destination[1]) * (255 - alpha) + 127) / 255));
                destination[2] = static_cast<std::uint8_t>(std::min(
                    255U,
                    static_cast<unsigned>(red) +
                        (static_cast<unsigned>(destination[2]) * (255 - alpha) + 127) / 255));
            }

            destination[3] = 255;
        }
    }
}

}  // namespace rdc::agent::platform::windows
