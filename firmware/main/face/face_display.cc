#include "face_display.h"

#include <algorithm>

#include <esp_log.h>
#include <esp_random.h>
#include <esp_timer.h>

#include "display/lvgl_display/lvgl_theme.h"

#define TAG "FaceDisplay"

namespace {

constexpr uint32_t kFramePeriodMs = 33;

// Face layout on a 320x240 screen, below the status bar and above subtitles.
constexpr int kEyeSpacing = 62;   // from face center to each eye center
constexpr int kEyeCenterY = 100;
constexpr int kEyeWidth = 54;
constexpr int kEyeMaxHeight = 66;
constexpr int kMouthCenterY = 165;

const lv_color_t kFeatureColor = lv_color_hex(0xFFFFFF);
const lv_color_t kBlushColor = lv_color_hex(0xFF6F91);

uint32_t NowMs() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

void FillRounded(lv_layer_t* layer, lv_color_t color, lv_opa_t opa, int x1, int y1, int x2,
                 int y2, int radius) {
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = color;
    dsc.bg_opa = opa;
    dsc.radius = radius;
    lv_area_t area = {x1, y1, x2, y2};
    lv_draw_rect(layer, &dsc, &area);
}

void Line(lv_layer_t* layer, int x1, int y1, int x2, int y2, int width) {
    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.color = kFeatureColor;
    dsc.width = width;
    dsc.round_start = 1;
    dsc.round_end = 1;
    dsc.p1.x = x1;
    dsc.p1.y = y1;
    dsc.p2.x = x2;
    dsc.p2.y = y2;
    lv_draw_line(layer, &dsc);
}

void Arc(lv_layer_t* layer, int cx, int cy, int radius, int start_deg, int end_deg, int width) {
    lv_draw_arc_dsc_t dsc;
    lv_draw_arc_dsc_init(&dsc);
    dsc.color = kFeatureColor;
    dsc.width = width;
    dsc.rounded = 1;
    dsc.center.x = cx;
    dsc.center.y = cy;
    dsc.radius = static_cast<uint16_t>(radius);
    dsc.start_angle = start_deg;
    dsc.end_angle = end_deg;
    lv_draw_arc(layer, &dsc);
}

}  // namespace

FaceDisplay::FaceDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                         int width, int height, int offset_x, int offset_y, bool mirror_x,
                         bool mirror_y, bool swap_xy, ExpressionArbiter& arbiter)
    : SpiLcdDisplay(panel_io, panel, width, height, offset_x, offset_y, mirror_x, mirror_y,
                    swap_xy),
      arbiter_(arbiter) {
    pose_ = nb_expression_params(NB_EXPR_NEUTRAL);
    nb_face_anim_init(&anim_, pose_, esp_random(), NowMs());
}

void FaceDisplay::SetupUI() {
    SpiLcdDisplay::SetupUI();
    SetTheme(LvglThemeManager::GetInstance().GetTheme("dark"));

    DisplayLockGuard lock(this);
    for (lv_obj_t* emoji : {emoji_box_, emoji_label_, emoji_image_}) {
        if (emoji != nullptr) {
            lv_obj_add_flag(emoji, LV_OBJ_FLAG_HIDDEN);
        }
    }

    lv_obj_t* screen = lv_screen_active();
    face_ = lv_obj_create(screen);
    lv_obj_remove_style_all(face_);
    lv_obj_set_size(face_, LV_PCT(100), LV_PCT(100));
    lv_obj_remove_flag(face_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_background(face_);
    lv_obj_add_event_cb(face_, OnDraw, LV_EVENT_DRAW_MAIN, this);
    lv_timer_create(OnTimer, kFramePeriodMs, this);
}

void FaceDisplay::SetEmotion(const char* emotion) {
    // The emoji is never drawn: the emotion becomes a proposal like any other.
    // xiaozhi sends "neutral" on most state changes to mean "no emotion", so
    // it withdraws the proposal instead of masking the state's expression.
    nb_expression_t expression = nb_expression_from_emotion(emotion);
    if (expression == NB_EXPR_NEUTRAL) {
        arbiter_.Withdraw(NB_SOURCE_EMOTION);
        return;
    }
    nb_proposal_t proposal = {expression, priority::kEmotion, 0, true};
    arbiter_.Propose(NB_SOURCE_EMOTION, proposal);
}

void FaceDisplay::OnTimer(lv_timer_t* timer) {
    static_cast<FaceDisplay*>(lv_timer_get_user_data(timer))->Tick();
}

void FaceDisplay::OnDraw(lv_event_t* event) {
    auto* self = static_cast<FaceDisplay*>(lv_event_get_user_data(event));
    lv_area_t area;
    lv_obj_get_coords(self->face_, &area);
    self->Draw(lv_event_get_layer(event), area);
}

void FaceDisplay::Tick() {
    nb_expression_t expression = arbiter_.Resolve();
    if (expression != shown_) {
        ESP_LOGI(TAG, "Expression: %s", nb_expression_name(expression));
        shown_ = expression;
        nb_face_anim_set_target(&anim_, nb_expression_params(expression));
    }
    nb_face_anim_set_speaking(&anim_, speaking_);
    pose_ = nb_face_anim_step(&anim_, NowMs());
    lv_obj_invalidate(face_);
}

void FaceDisplay::Draw(lv_layer_t* layer, const lv_area_t& area) {
    const int cx = (area.x1 + area.x2) / 2;
    const int top = area.y1;
    const nb_face_params_t& p = pose_;

    for (int side : {-1, 1}) {
        const int ex = cx + side * kEyeSpacing;
        const int ey = top + kEyeCenterY;
        const int h = std::max(4, static_cast<int>(kEyeMaxHeight * p.eye_open));
        const int x1 = ex - kEyeWidth / 2;
        const int x2 = ex + kEyeWidth / 2;
        const int y1 = ey - h / 2;
        const int y2 = ey + h / 2;
        FillRounded(layer, kFeatureColor, LV_OPA_COVER, x1, y1, x2, y2, std::min(kEyeWidth, h) / 2);

        // Smiling eyes: a background-colored lid pushes up from below.
        if (p.eye_smile > 0.05f && h > 8) {
            const int lid = static_cast<int>(h * 0.7f * p.eye_smile);
            FillRounded(layer, lv_color_black(), LV_OPA_COVER, x1 - 4, y2 - lid, x2 + 4,
                        y2 - lid + h, kEyeWidth / 2);
        }

        // Brows only when they say something; inner end follows brow_tilt.
        if (p.brow_tilt > 0.15f || p.brow_tilt < -0.15f) {
            const int by = y1 - 16;
            const int inner_dy = static_cast<int>(-p.brow_tilt * 10.0f);
            const int inner_x = ex - side * (kEyeWidth / 2);
            const int outer_x = ex + side * (kEyeWidth / 2);
            Line(layer, outer_x, by, inner_x, by + inner_dy, 6);
        }

        if (p.blush > 0.05f) {
            const int bx = ex + side * 20;
            const int by = ey + kEyeMaxHeight / 2 + 6;
            FillRounded(layer, kBlushColor, static_cast<lv_opa_t>(160 * p.blush), bx - 14, by - 7,
                        bx + 14, by + 7, 7);
        }
    }

    const int my = top + kMouthCenterY;
    if (p.mouth_open > 0.12f) {
        const int w = 26 + static_cast<int>(22 * p.mouth_open);
        const int h = 6 + static_cast<int>(36 * p.mouth_open);
        FillRounded(layer, kFeatureColor, LV_OPA_COVER, cx - w / 2, my - h / 2, cx + w / 2,
                    my + h / 2, std::min(w, h) / 2);
    } else if (p.mouth_curve > 0.1f || p.mouth_curve < -0.1f) {
        // Arc span grows with the curve; smiles bend down, frowns bend up.
        const float curve = p.mouth_curve > 0 ? p.mouth_curve : -p.mouth_curve;
        const int half_span = 20 + static_cast<int>(50 * curve);
        const int radius = 34;
        if (p.mouth_curve > 0) {
            Arc(layer, cx, my - radius + 8, radius, 90 - half_span, 90 + half_span, 6);
        } else {
            Arc(layer, cx, my + radius - 4, radius, 270 - half_span, 270 + half_span, 6);
        }
    } else {
        Line(layer, cx - 16, my, cx + 16, my, 6);
    }
}
