/**
 * @file desktop_frame.hpp
 * @brief 声明 agent/capture/desktop_frame 相关的类型、函数与流程。
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <wrl/client.h>

/**
 * @brief 描述桌面帧纹理及其相关元数据。
 */
struct ID3D11Texture2D;

namespace rdc::agent::capture {

/**
 * @brief 定义 DesktopFramePixelFormat 的枚举取值。
 */
enum class DesktopFramePixelFormat {
    Bgra8Unorm
};

/**
 * @brief 描述 DesktopDirtyRect 的矩形区域信息。
 */
struct DesktopDirtyRect {
    std::int32_t left = 0;
    std::int32_t top = 0;
    std::int32_t right = 0;
    std::int32_t bottom = 0;
};

/**
 * @brief 描述 DesktopMoveRect 的矩形区域信息。
 */
struct DesktopMoveRect {
    std::int32_t source_x = 0;
    std::int32_t source_y = 0;
    std::int32_t destination_left = 0;
    std::int32_t destination_top = 0;
    std::int32_t destination_right = 0;
    std::int32_t destination_bottom = 0;
};

/**
 * @brief 描述 DesktopFrame 的帧数据结构。
 */
struct DesktopFrame {
    std::int32_t desktop_left = 0;
    std::int32_t desktop_top = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    DesktopFramePixelFormat pixel_format = DesktopFramePixelFormat::Bgra8Unorm;
    std::int64_t present_qpc_ticks = 0;
    std::uint32_t accumulated_frames = 0;
    std::vector<DesktopDirtyRect> dirty_rects;
    std::vector<DesktopMoveRect> move_rects;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
};

/**
 * @brief 将输入值转换为字符串表示。
 * @param pixel_format 像素格式。
 * @return 返回生成的字符串结果。
 */
inline std::string ToString(const DesktopFramePixelFormat pixel_format) {
    switch (pixel_format) {
    case DesktopFramePixelFormat::Bgra8Unorm:
        return "bgra8_unorm";
    default:
        return "unknown";
    }
}

}  // namespace rdc::agent::capture
