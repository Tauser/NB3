#ifndef NB3_FACE_DISPLAY_H
#define NB3_FACE_DISPLAY_H

#include <memory>
#include <string>

#include <lvgl.h>

#include "behavior/expression_arbiter.h"
#include "core/nb_avatar.h"
#include "display/lcd_display.h"

// The NB3 screen on the ST7789. SpiLcdDisplay is used only for the panel,
// LVGL port and display lock; none of xiaozhi's layout is created. Every UI
// call xiaozhi makes lands here: emotions become arbiter proposals, the rest
// (status text, subtitles, notifications, previews) is not shown.
//
// The face is the NB avatar (core/nb_avatar), redrawn every frame from the
// expression the arbiter resolves.
class FaceDisplay : public SpiLcdDisplay {
public:
    FaceDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width,
                int height, int offset_x, int offset_y, bool mirror_x, bool mirror_y, bool swap_xy,
                ExpressionArbiter& arbiter);

    void SetupUI() override;
    void SetEmotion(const char* emotion) override;

    // xiaozhi UI hooks that NB3 does not render.
    void SetStatus(const char* status) override;
    void ShowNotification(const char* notification, int duration_ms = 3000) override;
    void ShowNotification(const std::string& notification, int duration_ms = 3000) override;
    void SetChatMessage(const char* role, const char* content) override;
    void ClearChatMessages() override {}
    void SetPreviewImage(std::unique_ptr<LvglImage>) override {}
    void UpdateStatusBar(bool = false) override {}
    void SetHideSubtitle(bool) override {}
    void SetTheme(Theme* theme) override;

private:
    static void OnTimer(lv_timer_t* timer);
    static void OnDraw(lv_event_t* event);
    void Tick();
    void Request(nb_expression_t expression);

    ExpressionArbiter& arbiter_;

    // Owned by the LVGL task.
    lv_obj_t* face_ = nullptr;
    nb_avatar_core_state_t avatar_;
    nb_avatar_render_frame_t frame_;
    nb_expression_t shown_ = NB_EXPR_NEUTRAL;
    uint32_t last_tick_ms_ = 0;
};

#endif  // NB3_FACE_DISPLAY_H
