/**
 * @file ui_widgets.cpp
 * @brief 实现 Windows GUI 常用控件与位图图标绘制辅助函数。
 */

#include "ui_widgets.hpp"

#include <cfloat>
#include <cstring>

#include "imgui_internal.h"

namespace rdc::ui::widgets {

namespace {

/**
 * @brief 保存下一帧需要重新聚焦并恢复光标位置的输入框请求。
 */
struct PendingEditableInputFocus {
    ImGuiID item_id = 0;
    float content_click_x = 0.0F;
};

/**
 * @brief 缓存挂起的文本输入焦点请求。
 */
PendingEditableInputFocus g_pending_editable_input_focus{};

/**
 * @brief 计算单行 UTF-8 文本在指定点击横坐标处对应的字节光标位置。
 * @param text UTF-8 文本内容。
 * @param click_offset_x 相对文本起点的点击横坐标。
 * @return 返回应设置的 UTF-8 字节光标位置。
 */
int ComputeCursorByteOffsetFromClick(const char* const text, const float click_offset_x) {
    if (text == nullptr || text[0] == '\0' || click_offset_x <= 0.0F) {
        return 0;
    }

    ImFont* const font = ImGui::GetFont();
    if (font == nullptr) {
        return static_cast<int>(std::strlen(text));
    }

    const char* const text_end = text + std::strlen(text);
    const float font_size = ImGui::GetFontSize();
    float consumed_width = 0.0F;

    for (const char* current = text; current < text_end;) {
        unsigned int codepoint = 0U;
        const int utf8_length = ImTextCharFromUtf8(&codepoint, current, text_end);
        if (utf8_length <= 0) {
            break;
        }

        const char* const next = current + utf8_length;
        const float glyph_width = font->CalcTextSizeA(font_size, FLT_MAX, 0.0F, current, next).x;
        if (click_offset_x < consumed_width + glyph_width * 0.5F) {
            return static_cast<int>(current - text);
        }

        consumed_width += glyph_width;
        if (click_offset_x < consumed_width) {
            return static_cast<int>(next - text);
        }

        current = next;
    }

    return static_cast<int>(text_end - text);
}

/**
 * @brief 计算当前输入框内容区中的点击横坐标。
 * @return 返回相对输入文本起点的点击横坐标。
 */
float ComputeEditableInputClickOffsetX() {
    const ImVec2 item_min = ImGui::GetItemRectMin();
    const ImGuiStyle& style = ImGui::GetStyle();
    return ImGui::GetIO().MouseClickedPos[ImGuiMouseButton_Left].x - item_min.x -
           style.FramePadding.x;
}

}  // namespace

/**
 * @brief 在绘制输入框前应用挂起的文本焦点请求。
 * @param item_id 输入框控件的 ImGui 标识。
 */
void PrepareEditableInputFocus(const ImGuiID item_id) {
    if (item_id == 0 || g_pending_editable_input_focus.item_id != item_id) {
        return;
    }

    ImGui::SetKeyboardFocusHere();
}

/**
 * @brief 在绘制输入框后处理点击定位与光标同步。
 * @param item_id 输入框控件的 ImGui 标识。
 * @param text 当前输入框使用的 UTF-8 文本内容。
 */
void FinalizeEditableInputInteraction(const ImGuiID item_id, const char* const text) {
    if (item_id == 0 || text == nullptr) {
        return;
    }

    const bool clicked_this_frame =
        ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        !ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    if (clicked_this_frame) {
        g_pending_editable_input_focus.item_id = item_id;
        g_pending_editable_input_focus.content_click_x = ComputeEditableInputClickOffsetX();
    }

    ImGuiInputTextState* const input_state = ImGui::GetInputTextState(item_id);
    if (input_state == nullptr) {
        return;
    }

    if (g_pending_editable_input_focus.item_id != item_id) {
        return;
    }

    const float click_offset_x = g_pending_editable_input_focus.content_click_x + input_state->Scroll.x;
    const int cursor_byte_offset = ComputeCursorByteOffsetFromClick(text, click_offset_x);
    input_state->SetSelection(cursor_byte_offset, cursor_byte_offset);
    input_state->CursorAnimReset();
    input_state->CursorFollow = true;
    g_pending_editable_input_focus = {};
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
bool LoadBitmapResourcePixels(const HINSTANCE hinstance,
                              const int resource_id,
                              std::vector<std::uint32_t>& out_pixels,
                              UINT& out_width,
                              UINT& out_height) {
    out_pixels.clear();
    out_width = 0;
    out_height = 0;

    const HBITMAP loaded_bitmap = static_cast<HBITMAP>(
        LoadImageW(hinstance,
                   MAKEINTRESOURCEW(resource_id),
                   IMAGE_BITMAP,
                   0,
                   0,
                   LR_CREATEDIBSECTION));
    if (loaded_bitmap == nullptr) {
        return false;
    }

    BITMAP bitmap_info{};
    if (GetObjectW(loaded_bitmap, sizeof(bitmap_info), &bitmap_info) == 0 ||
        bitmap_info.bmWidth <= 0 || bitmap_info.bmHeight <= 0) {
        DeleteObject(loaded_bitmap);
        return false;
    }

    out_width = static_cast<UINT>(bitmap_info.bmWidth);
    out_height = static_cast<UINT>(bitmap_info.bmHeight);
    out_pixels.resize(static_cast<std::size_t>(out_width) * static_cast<std::size_t>(out_height));

    BITMAPINFO dib_info{};
    dib_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    dib_info.bmiHeader.biWidth = static_cast<LONG>(out_width);
    dib_info.bmiHeader.biHeight = -static_cast<LONG>(out_height);
    dib_info.bmiHeader.biPlanes = 1;
    dib_info.bmiHeader.biBitCount = 32;
    dib_info.bmiHeader.biCompression = BI_RGB;

    HDC screen_dc = GetDC(nullptr);
    if (screen_dc == nullptr) {
        DeleteObject(loaded_bitmap);
        out_pixels.clear();
        out_width = 0;
        out_height = 0;
        return false;
    }

    const int copied_rows = GetDIBits(screen_dc,
                                      loaded_bitmap,
                                      0,
                                      out_height,
                                      out_pixels.data(),
                                      &dib_info,
                                      DIB_RGB_COLORS);
    ReleaseDC(nullptr, screen_dc);
    DeleteObject(loaded_bitmap);

    if (copied_rows == 0) {
        out_pixels.clear();
        out_width = 0;
        out_height = 0;
        return false;
    }

    for (std::uint32_t& pixel : out_pixels) {
        const std::uint8_t blue = static_cast<std::uint8_t>(pixel & 0xFFU);
        const std::uint8_t green = static_cast<std::uint8_t>((pixel >> 8U) & 0xFFU);
        const std::uint8_t red = static_cast<std::uint8_t>((pixel >> 16U) & 0xFFU);
        if (red >= 245U && green >= 245U && blue >= 245U) {
            pixel = 0U;
            continue;
        }

        pixel |= 0xFF000000U;
    }

    return true;
}

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
                    const ImVec2& icon_size) {
    if (draw_list == nullptr || !icon.IsValid() || icon_size.x <= 0.0F || icon_size.y <= 0.0F) {
        return;
    }

    const float scale =
        std::min(icon_size.x / static_cast<float>(icon.width),
                 icon_size.y / static_cast<float>(icon.height));
    if (scale <= 0.0F) {
        return;
    }

    const ImVec2 draw_size(static_cast<float>(icon.width) * scale,
                           static_cast<float>(icon.height) * scale);
    const ImVec2 draw_min(icon_min.x + (icon_size.x - draw_size.x) * 0.5F,
                          icon_min.y + (icon_size.y - draw_size.y) * 0.5F);
    const ImVec2 draw_max(draw_min.x + draw_size.x, draw_min.y + draw_size.y);

    draw_list->AddImage((ImTextureID)icon.gpu_descriptor.ptr, draw_min, draw_max);
}

/**
 * @brief 绘制左侧导航按钮。
 * @param id 控件唯一标识。
 * @param icon 待绘制的位图图标。
 * @param fallback_label 图标缺失时的回退文本。
 * @param selected 当前是否为选中状态。
 * @param size 按钮尺寸。
 * @return 返回是否被点击。
 */
bool DrawSidebarButton(const char* const id,
                       const BitmapIcon* const icon,
                       const char* const fallback_label,
                       const bool selected,
                       const ImVec2& size) {
    ImGui::PushID(id);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 14.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, selected ? 1.5F : 1.0F);
    ImGui::PushStyleColor(ImGuiCol_Button,
                          selected ? IM_COL32(238, 248, 255, 255)
                                   : IM_COL32(247, 251, 255, 240));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(232, 246, 255, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(221, 240, 255, 255));
    ImGui::PushStyleColor(ImGuiCol_Border,
                          IM_COL32(116, 187, 226, selected ? 255 : 190));
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(61, 92, 117, 255));

    const bool clicked = ImGui::Button("##sidebar_button", size);
    const ImVec2 item_min = ImGui::GetItemRectMin();
    const ImVec2 item_size = ImGui::GetItemRectSize();
    ImDrawList* const draw_list = ImGui::GetWindowDrawList();

    if (icon != nullptr && icon->IsValid()) {
        constexpr float kIconPadding = 7.0F;
        DrawBitmapIcon(draw_list,
                       *icon,
                       ImVec2(item_min.x + kIconPadding, item_min.y + kIconPadding),
                       ImVec2(item_size.x - kIconPadding * 2.0F,
                              item_size.y - kIconPadding * 2.0F));
    } else if (fallback_label != nullptr && fallback_label[0] != '\0') {
        const ImVec2 text_size = ImGui::CalcTextSize(fallback_label);
        draw_list->AddText(ImVec2(item_min.x + (item_size.x - text_size.x) * 0.5F,
                                  item_min.y + (item_size.y - text_size.y) * 0.5F),
                           IM_COL32(61, 92, 117, 255),
                           fallback_label);
    }

    ImGui::PopStyleColor(5);
    ImGui::PopStyleVar(2);
    ImGui::PopID();
    return clicked;
}

}  // namespace rdc::ui::widgets
