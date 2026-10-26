#include "nb_mood.h"

#include <math.h>

#define TWO_PI 6.28318530718f

typedef struct {
    uint8_t r, g, b;
    uint16_t period_ms;  // envelope period; 0 = steady
    uint8_t low;         // envelope floor, 0..255 of full level
    bool rainbow;        // ignore the color and cycle hues over period_ms
} mood_style_t;

//                                   r    g    b  period  low  rainbow
static const mood_style_t kStyles[NB_EXPR_COUNT] = {
    [NB_EXPR_NEUTRAL] = {120, 160, 255, 4000, 90, false},     // calm blue breathing
    [NB_EXPR_LISTENING] = {0, 220, 255, 0, 255, false},       // attentive cyan, steady
    [NB_EXPR_THINKING] = {150, 70, 255, 1600, 60, false},     // purple pulse
    [NB_EXPR_HAPPY] = {255, 190, 40, 2400, 170, false},       // warm yellow
    [NB_EXPR_EXCITED] = {255, 110, 0, 500, 80, false},        // orange, fast pulse
    [NB_EXPR_CURIOUS] = {0, 210, 150, 2000, 140, false},      // teal
    [NB_EXPR_CONFUSED] = {170, 255, 0, 1200, 100, false},     // lime
    [NB_EXPR_ANGRY] = {255, 0, 0, 350, 60, false},            // red, fast pulse
    [NB_EXPR_SURPRISED] = {255, 255, 255, 0, 255, false},     // white flash
    [NB_EXPR_SAD] = {20, 40, 200, 5000, 60, false},           // deep blue, slow
    [NB_EXPR_SLEEPY] = {80, 30, 140, 7000, 15, false},        // dim violet, very slow
    [NB_EXPR_DIZZY] = {255, 255, 255, 1500, 255, true},       // hue cycle
    [NB_EXPR_LOVING] = {255, 40, 120, 1800, 110, false},      // pink breathing
    [NB_EXPR_EMBARRASSED] = {255, 90, 110, 2600, 150, false}, // rose
};

static const mood_style_t *style_of(nb_expression_t expression) {
    if ((int)expression < 0 || expression >= NB_EXPR_COUNT) {
        expression = NB_EXPR_NEUTRAL;
    }
    return &kStyles[expression];
}

void nb_mood_init(nb_mood_t *mood, uint8_t max_brightness, uint32_t now_ms) {
    const mood_style_t *s = style_of(NB_EXPR_NEUTRAL);
    mood->expression = NB_EXPR_NEUTRAL;
    mood->r = s->r;
    mood->g = s->g;
    mood->b = s->b;
    mood->max_brightness = max_brightness;
    mood->last_ms = now_ms;
}

void nb_mood_set_expression(nb_mood_t *mood, nb_expression_t expression) {
    mood->expression = (int)expression >= 0 && expression < NB_EXPR_COUNT ? expression
                                                                           : NB_EXPR_NEUTRAL;
}

// Hue in [0, 1) to a fully saturated color.
static void hue_to_rgb(float hue, float *r, float *g, float *b) {
    float h = (hue - floorf(hue)) * 6.0f;
    float x = 1.0f - fabsf(fmodf(h, 2.0f) - 1.0f);
    float rr = 0, gg = 0, bb = 0;
    if (h < 1) { rr = 1; gg = x; }
    else if (h < 2) { rr = x; gg = 1; }
    else if (h < 3) { gg = 1; bb = x; }
    else if (h < 4) { gg = x; bb = 1; }
    else if (h < 5) { rr = x; bb = 1; }
    else { rr = 1; bb = x; }
    *r = rr * 255.0f;
    *g = gg * 255.0f;
    *b = bb * 255.0f;
}

static uint8_t to_byte(float v) {
    return v <= 0.0f ? 0 : v >= 255.0f ? 255 : (uint8_t)(v + 0.5f);
}

nb_rgb_t nb_mood_step(nb_mood_t *mood, uint32_t now_ms) {
    const mood_style_t *s = style_of(mood->expression);
    uint32_t dt = now_ms - mood->last_ms;
    mood->last_ms = now_ms;

    float tr = s->r, tg = s->g, tb = s->b;
    if (s->rainbow && s->period_ms > 0) {
        hue_to_rgb((float)(now_ms % s->period_ms) / (float)s->period_ms, &tr, &tg, &tb);
    }
    float k = 1.0f - expf(-(float)dt / NB_MOOD_EASE_MS);
    mood->r += (tr - mood->r) * k;
    mood->g += (tg - mood->g) * k;
    mood->b += (tb - mood->b) * k;

    float level = 1.0f;
    if (s->period_ms > 0 && !s->rainbow) {
        float phase = (float)(now_ms % s->period_ms) / (float)s->period_ms;
        float wave = 0.5f - 0.5f * cosf(TWO_PI * phase);  // 0..1, smooth
        float low = s->low / 255.0f;
        level = low + (1.0f - low) * wave;
    }
    float scale = level * (float)mood->max_brightness / 255.0f;
    nb_rgb_t out = {to_byte(mood->r * scale), to_byte(mood->g * scale), to_byte(mood->b * scale)};
    return out;
}
