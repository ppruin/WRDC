/**
 * @file ui_widgets.hpp
 * @brief 声明 Windows GUI 常用控件与位图图标绘制辅助函数。
 */

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <d3d12.h>
#include <windows.h>
#include <wrl/client.h>

#include "imgui.h"

namespace rdc::ui::widgets {

using Microsoft::WRL::ComPtr;

/**
 * @brief 描述一组待压栈的 ImGui 颜色样式项。
 */
struct WidgetColorStyle {
    ImGuiCol slot = ImGuiCol_Text;
    ImU32 color = 0U;
};

/**
 * @brief 将一组颜色样式批量压入 ImGui 样式栈。
 * @tparam ColorCount 颜色样式数量。
 * @param color_styles 待压入的颜色样式集合。
 */
template <std::size_t ColorCount>
void PushWidgetStyleColors(const std::array<WidgetColorStyle, ColorCount>& color_styles) {
    for (const WidgetColorStyle& color_style : color_styles) {
        ImGui::PushStyleColor(color_style.slot, color_style.color);
    }
}

/**
 * @brief 保存一个位图图标的 GPU 纹理与描述符句柄。
 */
struct BitmapIcon {
    UINT width = 0;
    UINT height = 0;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu_descriptor{};
    D3D12_GPU_DESCRIPTOR_HANDLE gpu_descriptor{};
    ComPtr<ID3D12Resource> texture{};

    /**
     * @brief 判断当前图标资源是否有效。
     * @return 返回是否成功或条件是否满足。
     */
    [[nodiscard]] bool IsValid() const noexcept {
        return width > 0 && height > 0 && cpu_descriptor.ptr != 0 && gpu_descriptor.ptr != 0 &&
               texture != nullptr;
    }
};

/**
 * @brief 在绘制输入框前应用挂起的文本焦点请求。
 * @param item_id 输入框控件的 ImGui 标识。
 */
void PrepareEditableInputFocus(ImGuiID item_id);

/**
 * @brief 在绘制输入框后处理点击定位与光标同步。
 * @param item_id 输入框控件的 ImGui 标识。
 * @param text 当前输入框使用的 UTF-8 文本内容。
 */
void FinalizeEditableInputInteraction(ImGuiID item_id, const char* text);

/**
 * @brief 绘制带标签的首页输入行。
 * @tparam BufferSize 输入缓冲区大小。
 * @param id 控件唯一标识。
 * @param label 左侧标签文本。
 * @param buffer 对应的输入缓冲区。
 * @param label_width 标签列宽度。
 * @param input_width 输入框宽度。
 * @param flags ImGui 输入框附加标志。
 * @return 返回是否发生了编辑。
 */
template <std::size_t BufferSize>
bool DrawLabeledInputRow(const char* id,
                         const char* label,
                         std::array<char, BufferSize>& buffer,
                         const float label_width,
                         const float input_width,
                         const ImGuiInputTextFlags flags = 0) {
    constexpr std::array<WidgetColorStyle, 4> kInputColors{
        WidgetColorStyle{ImGuiCol_FrameBg, IM_COL32(245, 247, 250, 255)},
        WidgetColorStyle{ImGuiCol_Border, IM_COL32(235, 239, 244, 255)},
        WidgetColorStyle{ImGuiCol_InputTextCursor, IM_COL32(32, 38, 48, 255)},
        WidgetColorStyle{ImGuiCol_Text, IM_COL32(32, 38, 48, 255)},
    };
    const bool is_editable = (flags & ImGuiInputTextFlags_ReadOnly) == 0;

    ImGui::PushID(id);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(label_width);

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 13.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0F);
    PushWidgetStyleColors(kInputColors);
    const ImGuiID input_id = ImGui::GetID("##value");
    if (is_editable) {
        PrepareEditableInputFocus(input_id);
    }

    ImGui::SetNextItemWidth(input_width);
    const bool edited = ImGui::InputText("##value", buffer.data(), buffer.size(), flags);
    if (is_editable) {
        FinalizeEditableInputInteraction(input_id, buffer.data());
    }

    ImGui::PopStyleColor(static_cast<int>(kInputColors.size()));
    ImGui::PopStyleVar(2);
    ImGui::PopID();
    return edited;
}

/**
 * @brief 绘制带标签的选择框输入行。
 * @tparam OptionCount 可选项数量。
 * @param id 控件唯一标识。
 * @param label 左侧标签文本。
 * @param selected_index 当前选中项索引。
 * @param option_labels 选择框选项文本集合。
 * @param label_width 标签列宽度。
 * @param input_width 选择框宽度。
 * @return 返回是否发生了选择变化。
 */
template <std::size_t OptionCount>
bool DrawLabeledComboRow(const char* id,
                         const char* label,
                         std::size_t& selected_index,
                         const std::array<const char*, OptionCount>& option_labels,
                         const float label_width,
                         const float input_width) {
    constexpr std::array<WidgetColorStyle, 4> kComboColors{
        WidgetColorStyle{ImGuiCol_FrameBg, IM_COL32(245, 247, 250, 255)},
        WidgetColorStyle{ImGuiCol_PopupBg, IM_COL32(255, 255, 255, 255)},
        WidgetColorStyle{ImGuiCol_Border, IM_COL32(235, 239, 244, 255)},
        WidgetColorStyle{ImGuiCol_Text, IM_COL32(32, 38, 48, 255)},
    };

    ImGui::PushID(id);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(label_width);

    selected_index = std::min(selected_index, OptionCount - 1);
    bool changed = false;
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 13.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0F);
    PushWidgetStyleColors(kComboColors);
    ImGui::SetNextItemWidth(input_width);

    if (ImGui::BeginCombo("##value", option_labels[selected_index])) {
        for (std::size_t option_index = 0; option_index < OptionCount; ++option_index) {
            const bool is_selected = option_index == selected_index;
            if (ImGui::Selectable(option_labels[option_index], is_selected)) {
                selected_index = option_index;
                changed = true;
            }

            if (is_selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    ImGui::PopStyleColor(static_cast<int>(kComboColors.size()));
    ImGui::PopStyleVar(2);
    ImGui::PopID();
    return changed;
}

/**
 * @brief 从 Windows 位图资源中读取像素数据。
 * @param hinstance 模块实例句柄。
 * @param resource_id 位图资源编号。
 * @param out_pixels 输出像素数组，格式为 BGRA8。
 * @param out_width 输出位图宽度。
 * @param out_height 输出位图高度。
 * @return 返回是否成功或条件是否满足。
 */
bool LoadBitmapResourcePixels(HINSTANCE hinstance,
                              int resource_id,
                              std::vector<std::uint32_t>& out_pixels,
                              UINT& out_width,
                              UINT& out_height);

/**
 * @brief 在指定区域内绘制位图图标。
 * @param draw_list 当前绘制列表。
 * @param icon 待绘制图标。
 * @param icon_min 图标区域左上角。
 * @param icon_size 图标区域大小。
 */
void DrawBitmapIcon(ImDrawList* draw_list,
                    const BitmapIcon& icon,
                    const ImVec2& icon_min,
                    const ImVec2& icon_size);

/**
 * @brief 绘制左侧导航按钮。
 * @param id 控件唯一标识。
 * @param icon 待绘制的位图图标。
 * @param fallback_label 图标缺失时的回退文本。
 * @param selected 当前是否为选中状态。
 * @param size 按钮尺寸。
 * @return 返回是否被点击。
 */
bool DrawSidebarButton(const char* id,
                       const BitmapIcon* icon,
                       const char* fallback_label,
                       bool selected,
                       const ImVec2& size);

}  // namespace rdc::ui::widgets
