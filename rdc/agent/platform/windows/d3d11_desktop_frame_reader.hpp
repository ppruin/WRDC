/**
 * @file d3d11_desktop_frame_reader.hpp
 * @brief 声明 agent/platform/windows/d3d11_desktop_frame_reader 相关的类型、函数与流程。
 */

#pragma once

#include <cstdint>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d11.h>
#include <wrl/client.h>

#include "../../capture/desktop_frame.hpp"
#include "../../encoder/raw_video_frame.hpp"

namespace rdc::agent::platform::windows {

/**
 * @brief 封装 D3D11DesktopFrameReader 相关的采集或读取流程。
 */
class D3D11DesktopFrameReader {
public:
    /**
     * @brief 析构 D3D11DesktopFrameReader 对象并释放相关资源。
     */
    ~D3D11DesktopFrameReader();

    /**
     * @brief 读取相关流程。
     * @param frame 视频帧对象。
     * @return 返回对应结果。
     */
    encoder::RawVideoFrame Read(const capture::DesktopFrame& frame);

private:
    /**
     * @brief 确保Context已就绪。
     * @param source_texture sourcetexture。
     */
    void EnsureContext(ID3D11Texture2D* source_texture);
    /**
     * @brief 确保StagingTexture已就绪。
     * @param source_texture sourcetexture。
     */
    void EnsureStagingTexture(ID3D11Texture2D* source_texture);
    /**
     * @brief 确保鼠标叠加用的 DIB Surface 已就绪。
     * @param width 光标位图宽度。
     * @param height 光标位图高度。
     */
    void EnsureCursorSurface(std::int32_t width, std::int32_t height);
    /**
     * @brief 将当前系统鼠标叠加到回读后的原始桌面帧上。
     * @param frame 桌面帧元数据。
     * @param raw_frame 原始视频帧数据。
     */
    void OverlayVisibleCursor(const capture::DesktopFrame& frame, encoder::RawVideoFrame& raw_frame);

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> device_context_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging_texture_;
    D3D11_TEXTURE2D_DESC staging_desc_{};
    HDC cursor_surface_dc_ = nullptr;
    HBITMAP cursor_surface_bitmap_ = nullptr;
    HGDIOBJ cursor_surface_old_bitmap_ = nullptr;
    void* cursor_surface_bits_ = nullptr;
    std::int32_t cursor_surface_width_ = 0;
    std::int32_t cursor_surface_height_ = 0;
};

}  // namespace rdc::agent::platform::windows
