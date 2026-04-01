/**
 * @file dxgi_desktop_duplicator.hpp
 * @brief 声明 agent/platform/windows/dxgi_desktop_duplicator 相关的类型、函数与流程。
 */

#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <vector>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include "../../capture/desktop_capturer.hpp"

namespace rdc::agent::platform::windows {

/**
 * @brief 封装 DxgiDesktopDuplicator 相关的采集或读取流程。
 */
class DxgiDesktopDuplicator final : public capture::DesktopCapturer {
public:
    /**
     * @brief 构造 DxgiDesktopDuplicator 对象。
     * @param output_index 显示输出索引。
     */
    explicit DxgiDesktopDuplicator(std::uint32_t output_index = 0);

    /**
     * @brief 采集Next帧。
     * @param timeout 等待超时时间。
     * @return 返回可用结果；失败时返回空值。
     */
    std::optional<capture::DesktopFrame> CaptureNextFrame(std::chrono::milliseconds timeout) override;

private:
    /**
     * @brief 初始化设备。
     */
    void InitializeDevice();
    /**
     * @brief 初始化Duplication。
     */
    void InitializeDuplication();
    /**
     * @brief 执行 `ReinitializeDuplication` 对应的处理逻辑。
     */
    void ReinitializeDuplication();
    /**
     * @brief 确保帧Texture已就绪。
     * @param source_texture sourcetexture。
     */
    void EnsureFrameTexture(ID3D11Texture2D* source_texture);
    /**
     * @brief 定位输出。
     * @param output_index 显示输出索引。
     * @return 返回对象指针或句柄。
     */
    Microsoft::WRL::ComPtr<IDXGIOutput1> ResolveOutput(std::uint32_t output_index) const;
    /**
     * @brief 复制帧。
     * @param source_texture sourcetexture。
     * @param frame_info frameinfo。
     * @return 返回对应结果。
     */
    capture::DesktopFrame CopyFrame(ID3D11Texture2D* source_texture, const DXGI_OUTDUPL_FRAME_INFO& frame_info);

    std::uint32_t output_index_;
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> device_context_;
    Microsoft::WRL::ComPtr<IDXGIOutput1> output_;
    Microsoft::WRL::ComPtr<IDXGIOutputDuplication> duplication_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> frame_texture_;
    D3D11_TEXTURE2D_DESC frame_texture_desc_{};
    DXGI_OUTPUT_DESC output_desc_{};
    std::vector<DXGI_OUTDUPL_MOVE_RECT> move_rect_buffer_;
    std::vector<RECT> dirty_rect_buffer_;
    DXGI_OUTDUPL_DESC duplication_desc_{};
};

}  // namespace rdc::agent::platform::windows
