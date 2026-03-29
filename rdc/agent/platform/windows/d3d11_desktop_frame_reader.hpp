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

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> device_context_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging_texture_;
    D3D11_TEXTURE2D_DESC staging_desc_{};
};

}  // namespace rdc::agent::platform::windows
