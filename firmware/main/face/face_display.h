#ifndef NB3_FACE_DISPLAY_H
#define NB3_FACE_DISPLAY_H

#include <atomic>

#include <lvgl.h>

#include "behavior/expression_arbiter.h"
#include "core/nb_face_anim.h"
#include "display/lcd_display.h"

// The NB3 face on the ST7789. xiaozhi still drives the status bar and the
// subtitles; the emoji area is replaced by an animated face that shows
// whatever the arbiter resolves. xiaozhi emotions become arbiter proposals.
class FaceDisplay : public SpiLcdDisplay {
public:
    FaceDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width,
                int height, int offset_x, int offset_y, bool mirror_x, bool mirror_y, bool swap_xy,
                ExpressionArbiter& arbiter);

    void SetupUI() override;
    void SetEmotion(const char* emotion) override;

    // Called from the device-state hook; read on the LVGL task.
    void SetSpeaking(bool speaking) { speaking_ = speaking; }

private:
    static void OnTimer(lv_timer_t* timer);
    static void OnDraw(lv_event_t* event);
    void Tick();
    void Draw(lv_layer_t* layer, const lv_area_t& area);

    ExpressionArbiter& arbiter_;
    std::atomic<bool> speaking_{false};

    // Owned by the LVGL task.
    lv_obj_t* face_ = nullptr;
    nb_face_anim_t anim_;
    nb_face_params_t pose_;
    nb_expression_t shown_ = NB_EXPR_NEUTRAL;
};

#endif  // NB3_FACE_DISPLAY_H
