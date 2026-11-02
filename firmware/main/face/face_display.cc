#include "face_display.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <esp_log.h>
#include <esp_random.h>
#include <esp_timer.h>

#include "application.h"
#include "assets/lang_config.h"
#include "core/nb_face_link.h"
#include "core/nb_overlay_anchor.h"
#include "display/lvgl_display/lvgl_theme.h"

#define TAG "FaceDisplay"

namespace {

// System banner layout. It sits at the bottom edge; four lines of text is the
// most it may grow to so the eyes stay clear for typical messages.
constexpr int kBannerMargin = 6;
constexpr int kBannerPad = 6;
constexpr int kBannerRadius = 10;
constexpr int kBannerMaxLines = 4;
constexpr lv_opa_t kBannerOpacity = 200;  // of 255
constexpr uint32_t kAlertNoticeMs = 6000;  // server alerts and upgrade messages

constexpr uint32_t kFramePeriodMs = 33;
constexpr uint32_t kFullRefreshMs = 10 * 1000;
constexpr float kPi = 3.14159265358979323846f;

// The avatar's 1000x1000 design space is scaled onto the 320x240 screen.
// 0.30 gives the eyes room to read while leaving margin for the vertical
// looks of the idle choreography.
constexpr float kDesignScale = 0.30f;

const lv_color_t kEyeColor = lv_color_hex(0xFFFFFF);
const lv_color_t kEdgeColor = lv_color_hex(0x7A7A7A);  // eye color at 48%: soft rim
const lv_color_t kBackground = lv_color_black();
// The only colours on the screen besides white (docs/FACE.md).
const lv_color_t kCheekColor = lv_color_hex(0xE8907A);  // salmon pills
const lv_color_t kHeartColor = lv_color_hex(0xFF7AAE);

// Overlay placement, in screen pixels from the eyes' rest centre, taken from
// the face V2 mocks (a 320x240 screen centred at 160,120). The painter moves
// and shrinks them with the eyes (core/nb_overlay_anchor).
constexpr float kBarsLeft = -35.5f;   // first bar's left edge
constexpr float kBarsStep = 11.0f;
constexpr float kBarsCentreY = 82.0f;
constexpr float kBarsMaxHeight = 32.0f;
constexpr float kHandLeft = -32.0f;   // hand image top-left, once in place
constexpr float kHandTop = 47.0f;
constexpr float kBubbleCentre[2] = {108.0f, -80.0f};
constexpr float kArcFrom = 0.3f;   // crescent depth from which an eye is drawn as an arc
constexpr int kArcSteps = 10;
constexpr float kHeartsY = -60.0f;    // where a heart starts rising
constexpr float kHeartsRise = 36.0f;
constexpr float kZStart[2] = {70.0f, -58.0f};  // where a Z appears, beside the eyes
constexpr float kZDrift[2] = {38.0f, -52.0f};  // how far it travels up and out

// What each channel draws, in the same units, for keeping it on the screen and
// clear of the banner. The hand image is 54x50 (hand_think_image.c).
constexpr float kHandW = 54.0f;
constexpr float kHandH = 50.0f;
constexpr nb_overlay_rect_t kBarsExtent = {kBarsLeft, kBarsCentreY - kBarsMaxHeight * 0.5f,
                                           kBarsLeft + (NB_OVERLAY_BARS - 1) * kBarsStep + 5.0f,
                                           kBarsCentreY + kBarsMaxHeight * 0.5f};
constexpr nb_overlay_rect_t kBubbleExtent = {kBubbleCentre[0] - 34.0f, kBubbleCentre[1] - 22.0f,
                                             kBubbleCentre[0] + 36.0f, kBubbleCentre[1] + 36.0f};
constexpr nb_overlay_rect_t kHandExtent = {kHandLeft, kHandTop, kHandLeft + kHandW,
                                           kHandTop + kHandH};
constexpr nb_overlay_rect_t kHeartsExtent = {-20.0f, kHeartsY - kHeartsRise - 14.0f, 22.0f,
                                             kHeartsY + 20.0f};
constexpr nb_overlay_rect_t kZsExtent = {kZStart[0] - 9.0f, kZStart[1] + kZDrift[1] - 9.0f,
                                         kZStart[0] + kZDrift[0] + 9.0f, kZStart[1] + 9.0f};
}  // namespace

// Generated from the user's hand drawing by tools/design/svg_to_lvgl.py.
extern "C" {
LV_IMAGE_DECLARE(nb_hand_think_image);
}

namespace {

uint32_t NowMs() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

float Clamp(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

lv_color_t Dim(lv_color_t color, float alpha) {
    return lv_color_make(static_cast<uint8_t>(color.red * alpha),
                         static_cast<uint8_t>(color.green * alpha),
                         static_cast<uint8_t>(color.blue * alpha));
}

// What a frame would draw, without drawing it: the box that holds every
// primitive and a hash of their parameters. The face is invalidated only
// where two consecutive frames differ, so LVGL redraws and flushes a part of
// the screen instead of all of it. Running the same painting code in
// recording mode keeps the box in step with what is really drawn.
struct Recorder {
    bool any = false;
    lv_area_t box = {0, 0, -1, -1};
    uint32_t hash = 2166136261u;

    void Mix(int32_t v) { hash = (hash ^ static_cast<uint32_t>(v)) * 16777619u; }
    void MixColor(lv_color_t c) {
        Mix(c.red);
        Mix(c.green);
        Mix(c.blue);
    }
    static int32_t Q(float v) { return static_cast<int32_t>(lroundf(v * 16.0f)); }
    void Box(int x0, int y0, int x1, int y1) {
        if (!any) {
            box = {x0, y0, x1, y1};
            any = true;
            return;
        }
        box.x1 = std::min<int32_t>(box.x1, x0);
        box.y1 = std::min<int32_t>(box.y1, y0);
        box.x2 = std::max<int32_t>(box.x2, x1);
        box.y2 = std::max<int32_t>(box.y2, y1);
    }
};

// Thin wrappers over the LVGL draw API, in screen pixels. With a recorder
// they only record.
struct Painter {
    lv_layer_t* layer;
    float cx;  // screen position of the design-space origin
    float cy;
    Recorder* rec = nullptr;

    void RoundRect(int x, int y, int w, int h, int radius, lv_color_t color) const {
        if (rec != nullptr) {
            rec->Box(x, y, x + w - 1, y + h - 1);
            rec->Mix(x);
            rec->Mix(y);
            rec->Mix(w);
            rec->Mix(h);
            rec->Mix(radius);
            rec->MixColor(color);
            return;
        }
        lv_draw_rect_dsc_t dsc;
        lv_draw_rect_dsc_init(&dsc);
        dsc.bg_color = color;
        dsc.bg_opa = LV_OPA_COVER;
        dsc.radius = radius;
        lv_area_t area = {x, y, x + w - 1, y + h - 1};
        lv_draw_rect(layer, &dsc, &area);
    }

    void Circle(float x, float y, int r, lv_color_t color) const {
        const int ix = static_cast<int>(lroundf(x));
        const int iy = static_cast<int>(lroundf(y));
        RoundRect(ix - r, iy - r, 2 * r + 1, 2 * r + 1, LV_RADIUS_CIRCLE, color);
    }

    void Line(float x0, float y0, float x1, float y1, float width, lv_color_t color,
              bool round = true) const {
        if (rec != nullptr) {
            const float pad = width * 0.5f + 2.0f;
            rec->Box(static_cast<int>(floorf(fminf(x0, x1) - pad)),
                     static_cast<int>(floorf(fminf(y0, y1) - pad)),
                     static_cast<int>(ceilf(fmaxf(x0, x1) + pad)),
                     static_cast<int>(ceilf(fmaxf(y0, y1) + pad)));
            rec->Mix(Recorder::Q(x0));
            rec->Mix(Recorder::Q(y0));
            rec->Mix(Recorder::Q(x1));
            rec->Mix(Recorder::Q(y1));
            rec->Mix(static_cast<int32_t>(lroundf(width)));
            rec->Mix(round ? 1 : 0);
            rec->MixColor(color);
            return;
        }
        lv_draw_line_dsc_t dsc;
        lv_draw_line_dsc_init(&dsc);
        dsc.color = color;
        dsc.width = static_cast<int32_t>(lroundf(width));
        dsc.round_start = round;
        dsc.round_end = round;
        dsc.p1.x = x0;
        dsc.p1.y = y0;
        dsc.p2.x = x1;
        dsc.p2.y = y1;
        lv_draw_line(layer, &dsc);
    }

    void Triangle(float x0, float y0, float x1, float y1, float x2, float y2,
                  lv_color_t color) const {
        if (rec != nullptr) {
            rec->Box(static_cast<int>(floorf(fminf(x0, fminf(x1, x2)) - 2.0f)),
                     static_cast<int>(floorf(fminf(y0, fminf(y1, y2)) - 2.0f)),
                     static_cast<int>(ceilf(fmaxf(x0, fmaxf(x1, x2)) + 2.0f)),
                     static_cast<int>(ceilf(fmaxf(y0, fmaxf(y1, y2)) + 2.0f)));
            for (const float v : {x0, y0, x1, y1, x2, y2}) {
                rec->Mix(Recorder::Q(v));
            }
            rec->MixColor(color);
            return;
        }
        lv_draw_triangle_dsc_t dsc;
        lv_draw_triangle_dsc_init(&dsc);
        dsc.color = color;
        dsc.opa = LV_OPA_COVER;
        dsc.p[0].x = x0;
        dsc.p[0].y = y0;
        dsc.p[1].x = x1;
        dsc.p[1].y = y1;
        dsc.p[2].x = x2;
        dsc.p[2].y = y2;
        lv_draw_triangle(layer, &dsc);
    }

    void Eye(const nb_avatar_eye_pose_t& eye, const nb_avatar_pose_t& pose, float blink) const {
        const float scale = kDesignScale * pose.face_scale;
        const float eye_x = cx + (pose.face_x + eye.x) * scale;
        const float eye_y = cy + (pose.face_y + eye.y) * scale;
        const int width = static_cast<int>(fmaxf(4.0f, roundf(eye.width * scale)));
        const int height = static_cast<int>(
            fmaxf(4.0f, roundf(eye.height * scale * Clamp(blink, 0.08f, 1.0f))));
        if (eye.heart > 0.5f) {
            // Loving: the eye is a heart. Heart() is anchored at its top edge, and its
            // body is centered 0.085 of its size below that.
            const float size = fmaxf(width, height) * 1.02f;
            Heart(eye_x, eye_y - size * 0.085f, size, kHeartColor);
            return;
        }
        const int left = static_cast<int>(roundf(eye_x - width * 0.5f));
        const int top = static_cast<int>(roundf(eye_y - height * 0.5f));
        if (fabsf(eye.crescent) >= kArcFrom) {
            ArcEye(eye_x, static_cast<float>(top), static_cast<float>(width),
                   static_cast<float>(height), eye.crescent);
            return;
        }
        const int radius = static_cast<int>(fmaxf(
            2.0f, roundf(fminf(width, height) * 0.5f * Clamp(eye.roundness, 0.0f, 1.0f))));

        // Rotated eyes are only drawn for plain round capsules; lids and
        // brows assume an upright eye.
        const bool rotated = fabsf(eye.angle) > 0.25f && eye.upper_lid < 0.01f &&
                             eye.lower_lid < 0.01f && eye.brow_opacity < 0.01f &&
                             fabsf(eye.crescent) < 0.01f && eye.roundness > 0.92f;
        if (rotated) {
            const float angle = eye.angle * kPi / 180.0f;
            const bool vertical = height >= width;
            const float major = static_cast<float>(vertical ? height : width);
            const float minor = static_cast<float>(vertical ? width : height);
            const float half_line = fmaxf(0.0f, major - minor) * 0.5f;
            const float axis_x = vertical ? -sinf(angle) : cosf(angle);
            const float axis_y = vertical ? cosf(angle) : sinf(angle);
            if (half_line < 0.5f) {
                const int r = static_cast<int>(fmaxf(2.0f, roundf(minor * 0.5f)));
                Circle(eye_x, eye_y, r + 1, kEdgeColor);
                Circle(eye_x, eye_y, r, kEyeColor);
            } else {
                // A capsule is a line with round caps as wide as the eye.
                const float x0 = eye_x - axis_x * half_line;
                const float y0 = eye_y - axis_y * half_line;
                const float x1 = eye_x + axis_x * half_line;
                const float y1 = eye_y + axis_y * half_line;
                Line(x0, y0, x1, y1, minor + 2.4f, kEdgeColor);
                Line(x0, y0, x1, y1, minor, kEyeColor);
            }
        } else {
            RoundRect(left - 1, top - 1, width + 2, height + 2, radius + 1, kEdgeColor);
            RoundRect(left, top, width, height, radius, kEyeColor);
        }

        const int upper_cover =
            static_cast<int>(roundf(Clamp(eye.upper_lid, 0.0f, 1.0f) * height));
        if (upper_cover > 0) {
            RoundRect(left - 1, top - 1, width + 2, upper_cover + 1, 0, kBackground);
        }
        const float upper_tilt = roundf(fabsf(eye.upper_lid_tilt) * height * 0.34f);
        if (upper_tilt > 0.0f) {
            const float lid_y = static_cast<float>(top + upper_cover);
            const float l = static_cast<float>(left);
            const float r = static_cast<float>(left + width);
            if (eye.upper_lid_tilt > 0.0f) {
                Triangle(l, lid_y, r, lid_y, r, lid_y + upper_tilt, kBackground);
                Line(l, lid_y, r, lid_y + upper_tilt, 1.0f, kEdgeColor, false);
            } else {
                Triangle(l, lid_y + upper_tilt, l, lid_y, r, lid_y, kBackground);
                Line(l, lid_y + upper_tilt, r, lid_y, 1.0f, kEdgeColor, false);
            }
        }
        const int lower_cover =
            static_cast<int>(roundf(Clamp(eye.lower_lid, 0.0f, 1.0f) * height));
        if (lower_cover > 0) {
            RoundRect(left - 1, top + height - lower_cover, width + 2, lower_cover + 1, 0,
                      kBackground);
        }

        // Crescent: a background circle flatter than the eye's own curve eats
        // the eye from below (a happy arc) or from above (a sleepy arc). The
        // eye keeps 1 - |crescent| of its height at the middle and thins out
        // to the sides. The circle stays clear of the other eye.
        const float crescent = Clamp(eye.crescent, -1.0f, 1.0f);
        if (fabsf(crescent) > 0.01f) {
            const int cut_r = static_cast<int>(roundf(fmaxf(width * 0.9f, width * 0.5f + 2.0f)));
            const int keep = static_cast<int>(roundf(height * (1.0f - fabsf(crescent))));
            if (crescent > 0.0f) {
                const int cut_top = top + keep;
                Circle(eye_x, static_cast<float>(cut_top + cut_r), cut_r, kBackground);
                const int rest = top + height + 2 - (cut_top + cut_r);
                if (rest > 0) {
                    RoundRect(left - 2, cut_top + cut_r, width + 4, rest, 0, kBackground);
                }
            } else {
                const int cut_bottom = top + height - keep;
                Circle(eye_x, static_cast<float>(cut_bottom - cut_r), cut_r, kBackground);
                const int rest = cut_bottom - cut_r - (top - 2);
                if (rest > 0) {
                    RoundRect(left - 2, top - 2, width + 4, rest, 0, kBackground);
                }
            }
        }

        const float brow_opacity = Clamp(eye.brow_opacity, 0.0f, 1.0f);
        if (brow_opacity > 0.01f) {
            const float length = eye.width * scale * 0.88f;
            const float radians = eye.brow_tilt * kPi / 180.0f;
            const float dx = cosf(radians) * length * 0.5f;
            const float dy = sinf(radians) * length * 0.5f;
            const float brow_y = eye_y + eye.brow_y * scale;
            const float thickness = fmaxf(2.0f, fminf(eye.width, eye.height) * scale * 0.04f);
            Line(eye_x - dx, brow_y - dy, eye_x + dx, brow_y + dy, thickness * 2.0f,
                 Dim(kEyeColor, 0.63f + 0.37f * brow_opacity));
        }
    }

    // A deep crescent is drawn as a thick arc with round ends (the happy arch, the
    // closed sleeping eye) instead of a capsule with a circle cut out of it, whose
    // ends would be sharp. The thickness is what the cut leaves at the middle.
    void ArcEye(float eye_x, float top, float width, float height, float crescent) const {
        const float thickness = fmaxf(3.0f, height * (1.0f - fabsf(crescent)));
        const float chord = fmaxf(1.0f, width - thickness);
        const float sag = fmaxf(1.0f, fminf(0.25f * width, height - thickness) * 0.9f);
        const float radius = (chord * chord * 0.25f + sag * sag) / (2.0f * sag);
        const float half_angle = asinf(fminf(1.0f, chord * 0.5f / radius));
        const bool arch = crescent > 0.0f;
        const float apex = arch ? top + thickness * 0.5f : top + height - thickness * 0.5f;
        const float centre_y = arch ? apex + radius : apex - radius;
        float prev_x = 0.0f;
        float prev_y = 0.0f;
        for (int pass = 0; pass < 2; ++pass) {
            const float w = pass == 0 ? thickness + 2.4f : thickness;
            const lv_color_t color = pass == 0 ? kEdgeColor : kEyeColor;
            for (int i = 0; i <= kArcSteps; ++i) {
                const float angle = -half_angle + 2.0f * half_angle * i / kArcSteps;
                const float x = eye_x + radius * sinf(angle);
                const float y = arch ? centre_y - radius * cosf(angle)
                                     : centre_y + radius * cosf(angle);
                if (i > 0) {
                    Line(prev_x, prev_y, x, y, w, color);
                }
                prev_x = x;
                prev_y = y;
            }
        }
    }

    void Heart(float x, float y, float size, lv_color_t color) const {
        const int r = static_cast<int>(fmaxf(2.0f, roundf(size * 0.28f)));
        Circle(x - size * 0.24f, y - size * 0.1f, r, color);
        Circle(x + size * 0.24f, y - size * 0.1f, r, color);
        Triangle(x - size * 0.5f, y, x + size * 0.5f, y, x, y + size * 0.55f, color);
    }

    // A small salmon pill under the outer corner of an eye, tilted.
    void Cheek(const nb_avatar_eye_pose_t& eye, const nb_avatar_pose_t& pose, float side,
               float size) const {
        const float scale = kDesignScale * pose.face_scale;
        const float x = cx + (pose.face_x + eye.x) * scale + side * (eye.width * scale * 0.5f - 2.0f);
        const float y = cy + (pose.face_y + eye.y) * scale + eye.height * scale * 0.3f;
        Line(x - 11.0f * size, y - 3.0f * size, x + 11.0f * size, y + 3.0f * size, 9.0f * size,
             kCheekColor);
    }

    void HandImage(float left, float top, float opacity, float angle, float scale) const {
        if (rec != nullptr) {
            // Turning around the pivot, the image stays within the farthest
            // corner's distance from it.
            const float w = static_cast<float>(nb_hand_think_image.header.w);
            const float h = static_cast<float>(nb_hand_think_image.header.h);
            const float px = left + w / 5.0f;
            const float py = top + h * 3.0f / 10.0f;
            const float reach =
                hypotf(fmaxf(px - left, left + w - px), fmaxf(py - top, top + h - py)) * scale + 2.0f;
            rec->Box(static_cast<int>(floorf(px - reach)), static_cast<int>(floorf(py - reach)),
                     static_cast<int>(ceilf(px + reach)), static_cast<int>(ceilf(py + reach)));
            rec->Mix(Recorder::Q(left));
            rec->Mix(Recorder::Q(top));
            rec->Mix(Recorder::Q(opacity));
            rec->Mix(Recorder::Q(angle));
            rec->Mix(Recorder::Q(scale));
            return;
        }
        lv_draw_image_dsc_t dsc;
        lv_draw_image_dsc_init(&dsc);
        dsc.src = &nb_hand_think_image;
        dsc.opa = static_cast<lv_opa_t>(Clamp(opacity, 0.0f, 1.0f) * 255.0f);
        dsc.rotation = static_cast<int32_t>(lroundf(angle * 10.0f));
        dsc.pivot.x = nb_hand_think_image.header.w / 5;  // the knuckles on the chin
        dsc.pivot.y = nb_hand_think_image.header.h * 3 / 10;
        dsc.scale_x = dsc.scale_y = static_cast<int32_t>(lroundf(scale * LV_SCALE_NONE));
        // LVGL scales around the pivot, so the knuckles stay at `left` + w/5.
        const int x = static_cast<int>(lroundf(left));
        const int y = static_cast<int>(lroundf(top));
        lv_area_t area = {x, y, x + static_cast<int>(nb_hand_think_image.header.w) - 1,
                          y + static_cast<int>(nb_hand_think_image.header.h) - 1};
        lv_draw_image(layer, &dsc, &area);
    }

    // A "Z" of the given height centred on (x, y): two bars and a diagonal.
    void Zee(float x, float y, float size, lv_color_t color) const {
        const float h = size * 0.5f;
        const float w = size * 0.45f;
        const float stroke = fmaxf(2.0f, size * 0.16f);
        Line(x - w, y - h, x + w, y - h, stroke, color);
        Line(x + w, y - h, x - w, y + h, stroke, color);
        Line(x - w, y + h, x + w, y + h, stroke, color);
    }

    // The face V2 overlay channels (docs/FACE.md): activity, then gesture; the
    // technical channel has no mark to draw yet. Fades are dims toward the
    // black background. Activity marks get a 1 px black outline so they stay
    // legible when they pass over an eye.
    void Overlay(const nb_overlay_frame_t& o, const ChannelLayout& layout,
                 const nb_avatar_pose_t& pose) const {
        nb_overlay_anchor_t pose_anchor;
        nb_overlay_anchor_from_pose(&pose, kDesignScale, &pose_anchor);
        if (o.cheeks) {
            Cheek(pose.left_eye, pose, -1.0f, pose_anchor.scale);
            Cheek(pose.right_eye, pose, 1.0f, pose_anchor.scale);
        }
        if (layout.activity_visible) {
            const float s0 = layout.activity.scale;
            const float ox = cx + layout.activity.dx;  // origin of the overlay layout
            const float oy = cy + layout.activity.dy;
            if (o.activity == NB_ACTIVITY_BARS) {
                for (int i = 0; i < NB_OVERLAY_BARS; ++i) {
                    const float h = fmaxf(4.0f, kBarsMaxHeight * o.bars[i] * s0);
                    const int x = static_cast<int>(lroundf(ox + (kBarsLeft + i * kBarsStep) * s0));
                    const int y = static_cast<int>(lroundf(oy + kBarsCentreY * s0 - h * 0.5f));
                    const int w = std::max(3, static_cast<int>(lroundf(5.0f * s0)));
                    const int ih = static_cast<int>(lroundf(h));
                    RoundRect(x - 1, y - 1, w + 2, ih + 2, 3, kBackground);
                    RoundRect(x, y, w, ih, 2, kEyeColor);
                }
            } else if (o.activity == NB_ACTIVITY_SPEECH && o.bubble >= 0.02f) {
                const float s = o.bubble * s0;
                const float bx = ox + kBubbleCentre[0] * s0;
                const float by = oy + kBubbleCentre[1] * s0;
                const int w = static_cast<int>(lroundf(72.0f * s));
                const int h = static_cast<int>(lroundf(44.0f * s));
                const int left = static_cast<int>(lroundf(bx - w * 0.5f));
                const int top = static_cast<int>(lroundf(by - h * 0.5f));
                const int radius = static_cast<int>(lroundf(16.0f * s));
                RoundRect(left - 1, top - 1, w + 2, h + 2, radius + 1, kBackground);
                Triangle(bx - 25.5f * s, by + 17.0f * s, bx - 36.5f * s, by + 38.0f * s,
                         bx - 4.5f * s, by + 20.0f * s, kBackground);
                RoundRect(left, top, w, h, radius, kEyeColor);
                // Tail pointing down-left toward the face.
                Triangle(bx - 24.0f * s, by + 18.0f * s, bx - 34.0f * s, by + 36.0f * s,
                         bx - 6.0f * s, by + 20.0f * s, kEyeColor);
                for (int i = 0; i < 3; ++i) {
                    Circle(bx + (i - 1) * 16.0f * s, by - o.dots[i] * 5.0f * s,
                           static_cast<int>(fmaxf(1.0f, roundf(5.0f * s))), kBackground);
                }
            }
        }
        if (layout.gesture_visible) {
            const float s0 = layout.gesture.scale;
            const float ox = cx + layout.gesture.dx;
            const float oy = cy + layout.gesture.dy;
            switch (o.gesture) {
                case NB_GESTURE_HAND:
                    if (o.hand > 0.01f) {
                        // The knuckles on the chin are the anchor point of the hand.
                        const float pivot_x =
                            static_cast<float>(nb_hand_think_image.header.w) / 5.0f;
                        const float pivot_y =
                            static_cast<float>(nb_hand_think_image.header.h) * 0.3f;
                        const float knuckle_x = ox + (kHandLeft + pivot_x) * s0;
                        const float knuckle_y =
                            oy + (kHandTop + (1.0f - o.hand) * 20.0f + pivot_y) * s0;
                        HandImage(knuckle_x - pivot_x, knuckle_y - pivot_y, o.hand, o.hand_angle,
                                  s0);
                    }
                    break;
                case NB_GESTURE_HEARTS:
                    // Loving shows its hearts as the eyes themselves (Eye()); the rising
                    // pair would only repeat them.
                    break;
                case NB_GESTURE_ZS:
                    for (int i = 0; i < NB_OVERLAY_ZS; ++i) {
                        const float p = o.zs[i];
                        if (p <= 0.0f) {
                            continue;
                        }
                        const float fade =
                            p < 0.2f ? p / 0.2f : (p > 0.7f ? (1.0f - p) / 0.3f : 1.0f);
                        Zee(ox + (kZStart[0] + kZDrift[0] * p) * s0,
                            oy + (kZStart[1] + kZDrift[1] * p) * s0, (9.0f + 9.0f * p) * s0,
                            Dim(kEyeColor, Clamp(fade, 0.0f, 1.0f)));
                    }
                    break;
                default:
                    break;
            }
        }
    }

    // Spinning rings over each eye plus a small zigzag, for DIZZY.
    void Dizzy(const nb_avatar_eye_pose_t& eye, const nb_avatar_pose_t& pose, int side,
               uint32_t now_ms) const {
        static constexpr float kRadiusScale[] = {0.74f, 0.58f, 0.43f, 0.29f, 0.14f};
        const float scale = kDesignScale * pose.face_scale;
        const float eye_x = cx + (pose.face_x + eye.x) * scale;
        const float eye_y = cy + (pose.face_y + eye.y) * scale;
        const float radius = fminf(eye.width, eye.height) * scale * 0.5f;
        if (radius < 8.0f) {
            return;
        }
        const float phase = (now_ms / 1000.0f) * 3.0f + (side > 0 ? kPi : 0.0f);
        for (int i = 0; i < 5; ++i) {
            const float orbit = radius * (0.025f + i * 0.014f);
            const float layer_phase = phase + i * 0.82f;
            const float x = eye_x + cosf(layer_phase) * orbit;
            const float y = eye_y + sinf(layer_phase) * orbit;
            const int r = static_cast<int>(fmaxf(2.0f, roundf(radius * kRadiusScale[i])));
            Circle(x, y, r + 1, kEdgeColor);
            Circle(x, y, r, (i % 2 == 0) ? kBackground : kEyeColor);
        }

        const uint32_t slot = (now_ms / 85u + (side > 0 ? 6u : 0u)) % 12u;
        if (slot > 1u) {
            return;
        }
        const float dir = side < 0 ? -1.0f : 1.0f;
        const float outer = fmaxf(eye.width, eye.height) * scale * 0.5f;
        const float x0 = eye_x + dir * outer * 0.72f;
        const float y0 = eye_y - outer * 0.48f;
        const float x1 = x0 + dir * 9, y1 = y0 - 8;
        const float x2 = x1 - dir * 4, y2 = y1 - 9;
        const float x3 = x2 + dir * 11, y3 = y2 - 9;
        Line(x0, y0, x1, y1, 2.0f, kEyeColor);
        Line(x1, y1, x2, y2, 2.0f, kEyeColor);
        Line(x2, y2, x3, y3, 2.0f, kEyeColor);
    }
};

// One frame of the face: for drawing, and again in recording mode to know
// where it will draw.
void PaintFace(const Painter& painter, const nb_avatar_render_frame_t& frame,
               const nb_overlay_frame_t& overlay, const ChannelLayout& channels) {
    if (!frame.enabled) {
        return;
    }
    painter.Eye(frame.pose.left_eye, frame.pose, frame.blink_scale);
    painter.Eye(frame.pose.right_eye, frame.pose, frame.blink_scale);
    painter.Overlay(overlay, channels, frame.pose);
    if (frame.dizzy_overlay) {
        painter.Dizzy(frame.pose.left_eye, frame.pose, -1, frame.animation_ms);
        painter.Dizzy(frame.pose.right_eye, frame.pose, 1, frame.animation_ms);
    }
}

}  // namespace

FaceDisplay::FaceDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                         int width, int height, int offset_x, int offset_y, bool mirror_x,
                         bool mirror_y, bool swap_xy, ExpressionArbiter& arbiter,
                         MoodModifier& mood, const AudioLevels& levels)
    : SpiLcdDisplay(panel_io, panel, width, height, offset_x, offset_y, mirror_x, mirror_y,
                    swap_xy),
      arbiter_(arbiter),
      mood_(mood),
      levels_(levels) {
    nb_overlay_init(&overlay_);
    nb_banner_init(&banner_);
    nb_avatar_core_init(&avatar_, esp_random());
    nb_gaze_init(&gaze_, esp_random());
    nb_avatar_core_get_frame(&avatar_, &frame_);
}

void FaceDisplay::SetupUI() {
    // Skip LcdDisplay::SetupUI(): the screen layout is NB3's alone.
    Display::SetupUI();

    DisplayLockGuard lock(this);
    lv_obj_t* screen = lv_screen_active();
    lv_obj_clean(screen);
    lv_obj_set_style_bg_color(screen, kBackground, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    face_ = lv_obj_create(screen);
    lv_obj_remove_style_all(face_);
    lv_obj_set_size(face_, LV_PCT(100), LV_PCT(100));
    lv_obj_remove_flag(face_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(face_, OnDraw, LV_EVENT_DRAW_MAIN, this);
    // Created after the face so it draws on top; hidden until a message comes.
    banner_box_ = lv_obj_create(screen);
    lv_obj_remove_style_all(banner_box_);
    lv_obj_set_width(banner_box_, lv_display_get_horizontal_resolution(nullptr) - 2 * kBannerMargin);
    lv_obj_set_height(banner_box_, LV_SIZE_CONTENT);
    lv_obj_align(banner_box_, LV_ALIGN_BOTTOM_MID, 0, -kBannerMargin);
    lv_obj_set_style_bg_color(banner_box_, kBackground, 0);
    lv_obj_set_style_bg_opa(banner_box_, kBannerOpacity, 0);
    lv_obj_set_style_radius(banner_box_, kBannerRadius, 0);
    lv_obj_set_style_pad_all(banner_box_, kBannerPad, 0);
    lv_obj_remove_flag(banner_box_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(banner_box_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(banner_box_, LV_OBJ_FLAG_HIDDEN);

    banner_label_ = lv_label_create(banner_box_);
    lv_obj_set_width(banner_label_, LV_PCT(100));
    lv_label_set_long_mode(banner_label_, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_color(banner_label_, kEyeColor, 0);
    lv_obj_set_style_text_align(banner_label_, LV_TEXT_ALIGN_CENTER, 0);

    last_tick_ms_ = NowMs();
    lv_timer_create(OnTimer, kFramePeriodMs, this);
}

void FaceDisplay::SetEmotion(const char* emotion) {
    // The emoji is never drawn: the emotion becomes a proposal like any other,
    // and a push on the mood.
    // xiaozhi sends "neutral" on most state changes to mean "no emotion", so
    // it withdraws the proposal instead of masking the state's expression.
    nb_expression_t expression = nb_expression_from_emotion(emotion);
    if (expression == NB_EXPR_NEUTRAL) {
        arbiter_.Withdraw(NB_SOURCE_EMOTION);
        return;
    }
    nb_proposal_t proposal = {expression, priority::kEmotion, 0, true};
    arbiter_.Propose(NB_SOURCE_EMOTION, proposal);
    mood_.OnEmotion(expression);
}

void FaceDisplay::ShowBanner(nb_banner_kind_t kind, const char* text, uint32_t timeout_ms) {
    std::lock_guard<std::mutex> lock(banner_mutex_);
    nb_banner_show(&banner_, kind, text, timeout_ms, NowMs());
}

// Application::Alert() calls SetStatus(title), SetEmotion, then
// SetChatMessage("system", message), so the status text says what kind of
// message the next system chat line is. Entering standby/listening/speaking
// means normal operation: whatever banner was up is no longer true.
void FaceDisplay::SetStatus(const char* status) {
    if (status == nullptr) {
        return;
    }
    ESP_LOGI(TAG, "Status: %s", status);
    std::lock_guard<std::mutex> lock(banner_mutex_);
    last_status_ = status;
    if (strcmp(status, Lang::Strings::STANDBY) == 0 ||
        strcmp(status, Lang::Strings::LISTENING) == 0 ||
        strcmp(status, Lang::Strings::SPEAKING) == 0) {
        nb_banner_clear(&banner_);
    }
}

void FaceDisplay::ShowNotification(const char* notification, int duration_ms) {
    if (notification == nullptr) {
        return;
    }
    ESP_LOGI(TAG, "Notification: %s", notification);
    ShowBanner(NB_BANNER_NOTICE, notification, duration_ms > 0 ? duration_ms : 0);
}

void FaceDisplay::ShowNotification(const std::string& notification, int duration_ms) {
    ShowNotification(notification.c_str(), duration_ms);
}

// Only "system" text is a banner candidate; what the user and the assistant
// say is conversation, not shown. A system message before the first status
// (the boot user-agent line) is not worth the screen either.
void FaceDisplay::SetChatMessage(const char* role, const char* content) {
    if (role == nullptr || content == nullptr || content[0] == '\0') {
        return;
    }
    ESP_LOGI(TAG, "%s: %s", role, content);
    if (strcmp(role, "system") != 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(banner_mutex_);
    if (last_status_.empty()) {
        return;
    }
    const char* status = last_status_.c_str();
    if (strcmp(status, Lang::Strings::ACTIVATION) == 0) {
        nb_banner_show(&banner_, NB_BANNER_ACTIVATION, content, 0, NowMs());
    } else if (strcmp(status, Lang::Strings::WIFI_CONFIG_MODE) == 0) {
        nb_banner_show(&banner_, NB_BANNER_WIFI_SETUP, content, 0, NowMs());
    } else if (strcmp(status, Lang::Strings::ERROR) == 0) {
        nb_banner_show(&banner_, NB_BANNER_ERROR, content, 0, NowMs());
        errors_seen_++;
    } else {
        nb_banner_show(&banner_, NB_BANNER_NOTICE, content, kAlertNoticeMs, NowMs());
    }
}

void FaceDisplay::SetTheme(Theme* theme) {
    // The banner takes its text font from the theme (PT-BR accents); colors
    // and the rest of the theme are unused. xiaozhi swaps themes when assets
    // reload and may free the old fonts, so only a shared reference is taken
    // here; the LVGL task adopts it in UpdateBanner() while banner_font_
    // still keeps the previous font alive.
    current_theme_ = theme;
    if (theme == nullptr) {
        return;
    }
    auto font = static_cast<LvglTheme*>(theme)->text_font();
    if (font != nullptr) {
        std::lock_guard<std::mutex> lock(banner_mutex_);
        pending_font_ = std::move(font);
    }
}

void FaceDisplay::SetEnergy(float arousal) {
    energy_arousal_ = arousal;
}

bool FaceDisplay::FaceLook(float* yaw, float* pitch) const {
    *yaw = look_yaw_;
    *pitch = look_pitch_;
    return look_own_;
}

void FaceDisplay::SetArc(bool active, const nb_avatar_look_t& look, float eye_open) {
    std::lock_guard<std::mutex> lock(arc_mutex_);
    arc_active_ = active;
    arc_look_ = look;
    arc_eye_open_ = eye_open;
}

void FaceDisplay::OnTimer(lv_timer_t* timer) {
    static_cast<FaceDisplay*>(lv_timer_get_user_data(timer))->Tick();
}

void FaceDisplay::Tick() {
    nb_source_t winner = NB_SOURCE_COUNT;
    nb_expression_t expression = arbiter_.Resolve(&winner);
    if (expression != shown_) {
        ESP_LOGI(TAG, "Expression: %s", nb_expression_name(expression));
        shown_ = expression;
        nb_face_link_show(&avatar_, expression);
    }
    const uint32_t now = NowMs();
    // At rest (nothing proposed, or only the mood) the robot looks around in
    // the manner of its mood; any reaction brings the eyes back to the user.
    // A tired robot looks around as calmly as its energy, whatever the mood.
    const nb_affect_vec_t mood = mood_.Mood();
    const bool sleeping = sleeping_;
    nb_gaze_set_mood(&gaze_, mood.valence, std::min(mood.arousal, energy_arousal_.load()));
    nb_gaze_tick(&gaze_, now - last_tick_ms_);
    const bool wander = !sleeping && (winner == NB_SOURCE_COUNT || winner == NB_SOURCE_MOOD ||
                                      winner == NB_SOURCE_ENERGY);
    nb_avatar_core_set_eyes_closed(&avatar_, sleeping);
    // An arc that wins brings its own look and slow blink.
    bool arc;
    nb_avatar_look_t arc_look;
    float arc_eye_open;
    {
        std::lock_guard<std::mutex> lock(arc_mutex_);
        arc = arc_active_ && winner == NB_SOURCE_ARC;
        arc_look = arc_look_;
        arc_eye_open = arc_eye_open_;
    }
    const nb_avatar_look_t look = arc ? arc_look : nb_gaze_look(&gaze_);
    nb_avatar_core_set_look(&avatar_, &look, wander || arc);
    look_yaw_ = look.yaw;
    look_pitch_ = look.pitch;
    look_own_ = wander || arc;
    nb_avatar_core_set_blink_pace(&avatar_, wander ? nb_gaze_blink_pace(&gaze_) : 1.0f);
    nb_avatar_core_set_blink_override(&avatar_, arc_eye_open, arc && arc_eye_open < 1.0f);
    nb_avatar_core_tick(&avatar_, now - last_tick_ms_);
    last_tick_ms_ = now;
    nb_avatar_core_get_frame(&avatar_, &frame_);
    // The banner first, so the channels are placed around where it really is.
    UpdateBanner();
    const DeviceState device = Application::GetInstance().GetDeviceState();
    nb_overlay_input_t channels_in = {};
    channels_in.winner = expression;
    channels_in.listening = device == kDeviceStateListening;
    channels_in.voice = Application::GetInstance().GetAudioService().IsVoiceDetected();
    channels_in.speaking = device == kDeviceStateSpeaking;
    channels_in.sleeping = sleeping;
    // No producer yet for a confirmed wait or the technical marks.
    channels_in.audio_level = channels_in.speaking ? levels_.Speaker(now) : levels_.Mic(now);
    nb_overlay_update(&overlay_, &channels_in, now, &overlay_frame_);
    PlaceChannels();
    InvalidateChanged(now);
    ticks_++;
}

// Anchors each channel to the eyes, keeps it on the screen and hides it where
// the banner (its real height, measured by LVGL) covers it.
void FaceDisplay::PlaceChannels() {
    const float screen_w = static_cast<float>(lv_display_get_horizontal_resolution(nullptr));
    const float screen_h = static_cast<float>(lv_display_get_vertical_resolution(nullptr));
    float banner_top = screen_h;
    if (banner_shown_) {
        lv_obj_update_layout(banner_box_);
        lv_area_t area;
        lv_obj_get_coords(banner_box_, &area);
        banner_top = static_cast<float>(area.y1);
    }
    nb_overlay_anchor_t pose_anchor;
    nb_overlay_anchor_from_pose(&frame_.pose, kDesignScale, &pose_anchor);

    const nb_overlay_rect_t* activity_extent = nullptr;
    if (overlay_frame_.activity == NB_ACTIVITY_BARS) {
        activity_extent = &kBarsExtent;
    } else if (overlay_frame_.activity == NB_ACTIVITY_SPEECH) {
        activity_extent = &kBubbleExtent;
    }
    channels_.activity = pose_anchor;
    channels_.activity_visible = true;
    if (activity_extent != nullptr) {
        nb_overlay_clamp_anchor(activity_extent, screen_w, screen_h, &channels_.activity);
        channels_.activity_visible = !nb_overlay_hidden_by_banner(
            activity_extent, &channels_.activity, screen_h, banner_top);
    }

    const nb_overlay_rect_t* gesture_extent = nullptr;
    switch (overlay_frame_.gesture) {
        case NB_GESTURE_HAND: gesture_extent = &kHandExtent; break;
        case NB_GESTURE_HEARTS: gesture_extent = &kHeartsExtent; break;
        case NB_GESTURE_ZS: gesture_extent = &kZsExtent; break;
        default: break;
    }
    channels_.gesture = pose_anchor;
    channels_.gesture_visible = true;
    if (gesture_extent != nullptr) {
        nb_overlay_clamp_anchor(gesture_extent, screen_w, screen_h, &channels_.gesture);
        channels_.gesture_visible = !nb_overlay_hidden_by_banner(
            gesture_extent, &channels_.gesture, screen_h, banner_top);
    }
}

// Runs on the LVGL task. Takes the lock only to expire the banner and copy
// its text out, and touches the objects only when something changed.
void FaceDisplay::UpdateBanner() {
    char text[NB_BANNER_TEXT_MAX];
    bool visible;
    uint32_t revision;
    std::shared_ptr<LvglFont> new_font;
    {
        std::lock_guard<std::mutex> lock(banner_mutex_);
        new_font = std::move(pending_font_);
        pending_font_.reset();
        visible = nb_banner_update(&banner_, NowMs());
        revision = banner_.revision;
        if (visible && (!banner_shown_ || revision != banner_shown_revision_)) {
            memcpy(text, banner_.text, sizeof(text));
        } else {
            text[0] = '\0';
        }
    }

    // The label's style holds a raw lv_font_t*, so the font object must stay
    // alive for as long as the style points at it: banner_font_ owns it, and
    // the previous owner is released only after the style points at the new
    // font (the old reference drops when new_font replaces it below).
    if (new_font != nullptr && new_font != banner_font_ && new_font->font() != nullptr) {
        const lv_font_t* font = new_font->font();
        lv_obj_set_style_text_font(banner_label_, font, 0);
        lv_obj_set_style_max_height(
            banner_box_, lv_font_get_line_height(font) * kBannerMaxLines + 2 * kBannerPad, 0);
        banner_font_ = std::move(new_font);
    }

    if (!visible) {
        if (banner_shown_) {
            lv_obj_add_flag(banner_box_, LV_OBJ_FLAG_HIDDEN);
            banner_shown_ = false;
        }
        return;
    }
    if (text[0] == '\0') {
        return;  // already showing this revision
    }

    lv_label_set_text(banner_label_, text);
    lv_obj_remove_flag(banner_box_, LV_OBJ_FLAG_HIDDEN);
    banner_shown_ = true;
    banner_shown_revision_ = revision;
}

void FaceDisplay::OnDraw(lv_event_t* event) {
    auto* self = static_cast<FaceDisplay*>(lv_event_get_user_data(event));
    if (!self->frame_.enabled) {
        return;
    }
    const int64_t started = esp_timer_get_time();
    lv_area_t area;
    lv_obj_get_coords(self->face_, &area);
    const Painter painter = {lv_event_get_layer(event), (area.x1 + area.x2 + 1) * 0.5f,
                             (area.y1 + area.y2 + 1) * 0.5f};
    PaintFace(painter, self->frame_, self->overlay_frame_, self->channels_);
    const uint32_t us = static_cast<uint32_t>(esp_timer_get_time() - started);
    self->draw_frames_++;
    self->draw_us_sum_ += us;
    uint32_t seen = self->draw_us_max_;
    while (us > seen && !self->draw_us_max_.compare_exchange_weak(seen, us)) {
    }
}

// Asks LVGL to redraw only where this frame differs from the one on the
// screen: the box of what it will draw together with the box of the last one
// (to wipe what has moved away). Nothing at all when the frame draws the same
// thing. Every few seconds the whole face is redrawn anyway, so a mistake in
// the boxes could only ever show as a short-lived smudge.
void FaceDisplay::InvalidateChanged(uint32_t now) {
    lv_area_t face;
    lv_obj_get_coords(face_, &face);
    Recorder rec;
    const Painter painter = {nullptr, (face.x1 + face.x2 + 1) * 0.5f, (face.y1 + face.y2 + 1) * 0.5f,
                             &rec};
    PaintFace(painter, frame_, overlay_frame_, channels_);

    const bool same = have_drawn_ && rec.any == drawn_any_ && rec.hash == drawn_hash_ &&
                      (!rec.any || (rec.box.x1 == drawn_box_.x1 && rec.box.y1 == drawn_box_.y1 &&
                                    rec.box.x2 == drawn_box_.x2 && rec.box.y2 == drawn_box_.y2));
    const bool refresh = !have_drawn_ || now - last_full_ms_ >= kFullRefreshMs;
    if (same && !refresh) {
        return;
    }
    if (refresh) {
        lv_obj_invalidate(face_);
        last_full_ms_ = now;
    } else {
        lv_area_t dirty = rec.box;
        bool any = rec.any;
        if (drawn_any_) {
            if (any) {
                dirty.x1 = std::min<int32_t>(dirty.x1, drawn_box_.x1);
                dirty.y1 = std::min<int32_t>(dirty.y1, drawn_box_.y1);
                dirty.x2 = std::max<int32_t>(dirty.x2, drawn_box_.x2);
                dirty.y2 = std::max<int32_t>(dirty.y2, drawn_box_.y2);
            } else {
                dirty = drawn_box_;
                any = true;
            }
        }
        if (any) {
            lv_obj_invalidate_area(face_, &dirty);
        }
    }
    have_drawn_ = true;
    drawn_any_ = rec.any;
    drawn_box_ = rec.box;
    drawn_hash_ = rec.hash;
}

FaceDisplay::DrawStats FaceDisplay::TakeDrawStats() {
    DrawStats s;
    s.ticks = ticks_.exchange(0);
    s.frames = draw_frames_.exchange(0);
    const uint32_t sum = draw_us_sum_.exchange(0);
    s.avg_us = s.frames > 0 ? sum / s.frames : 0;
    s.max_us = draw_us_max_.exchange(0);
    return s;
}
