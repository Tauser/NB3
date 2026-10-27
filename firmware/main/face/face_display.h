#ifndef NB3_FACE_DISPLAY_H
#define NB3_FACE_DISPLAY_H

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

#include <lvgl.h>

#include "behavior/expression_arbiter.h"
#include "behavior/mood_modifier.h"
#include "core/nb_overlay.h"
#include "face/audio_levels.h"
#include "core/nb_avatar.h"
#include "core/nb_gaze.h"
#include "core/nb_banner.h"
#include "display/lcd_display.h"
#include "display/lvgl_display/lvgl_font.h"

// The NB3 screen on the ST7789. SpiLcdDisplay is used only for the panel,
// LVGL port and display lock; none of xiaozhi's layout is created. Every UI
// call xiaozhi makes lands here: emotions become arbiter proposals, system
// messages (Wi-Fi setup, activation code, errors, notifications) go to the
// banner, and the rest (status bar, chat subtitles, previews) is not shown.
//
// The face is the NB avatar (core/nb_avatar), redrawn every frame from the
// expression the arbiter resolves, with at most one overlay on top
// (core/nb_overlay: listening bars, thought dots and hand, speech bubble,
// hearts, cheeks).
class FaceDisplay : public SpiLcdDisplay {
public:
    FaceDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width,
                int height, int offset_x, int offset_y, bool mirror_x, bool mirror_y, bool swap_xy,
                ExpressionArbiter& arbiter, MoodModifier& mood, const AudioLevels& levels);

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

    // From the energy: the calmest arousal the gaze may show, and sleep
    // (eyes nearly shut, no looking around).
    void SetEnergy(float arousal, bool sleeping);

private:
    static void OnTimer(lv_timer_t* timer);
    static void OnDraw(lv_event_t* event);
    void Tick();
    void ShowBanner(nb_banner_kind_t kind, const char* text, uint32_t timeout_ms);
    void UpdateBanner();

    ExpressionArbiter& arbiter_;
    MoodModifier& mood_;
    const AudioLevels& levels_;
    std::atomic<float> energy_arousal_{1.0f};
    std::atomic<bool> sleeping_{false};

    // Owned by the LVGL task.
    lv_obj_t* face_ = nullptr;
    nb_avatar_core_state_t avatar_;
    nb_gaze_t gaze_;  // LVGL task only
    nb_avatar_render_frame_t frame_;
    nb_expression_t shown_ = NB_EXPR_NEUTRAL;
    uint32_t last_tick_ms_ = 0;
    nb_overlay_t overlay_;
    nb_overlay_frame_t overlay_frame_ = {};

    // System banner. xiaozhi calls arrive from other tasks and only touch
    // banner_ and last_status_ under banner_mutex_ (held for microseconds);
    // the LVGL task copies the result out in Tick() and is the only one to
    // touch the objects below.
    std::mutex banner_mutex_;
    nb_banner_t banner_;
    std::string last_status_;  // last SetStatus text: says what a "system" message is
    std::shared_ptr<LvglFont> pending_font_;  // from SetTheme, under banner_mutex_
    std::shared_ptr<LvglFont> banner_font_;   // LVGL task only: the font the label points at
    lv_obj_t* banner_box_ = nullptr;
    lv_obj_t* banner_label_ = nullptr;
    uint32_t banner_shown_revision_ = 0;
    bool banner_shown_ = false;
};

#endif  // NB3_FACE_DISPLAY_H
