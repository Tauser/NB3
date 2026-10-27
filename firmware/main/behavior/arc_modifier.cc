#include "arc_modifier.h"

#include <esp_log.h>
#include <esp_random.h>

#define TAG "Arc"

namespace {

constexpr uint64_t kTickPeriodUs = 50 * 1000;
// The proposal outlives the arc by this much, so a missed end still ends it.
constexpr uint32_t kSafetyMs = 1000;

uint32_t NowMs() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

}  // namespace

ArcModifier::ArcModifier(ExpressionArbiter& arbiter, BehaviorModifier& behavior,
                         MoodModifier& mood, EnergyModifier& energy, FaceDisplay* display)
    : arbiter_(arbiter), behavior_(behavior), mood_(mood), energy_(energy), display_(display) {
    nb_arc_init(&arc_, esp_random());
    esp_timer_create_args_t args = {};
    args.callback = OnTimer;
    args.arg = this;
    args.name = "nb_arc";
    if (esp_timer_create(&args, &timer_) != ESP_OK ||
        esp_timer_start_periodic(timer_, kTickPeriodUs) != ESP_OK) {
        ESP_LOGE(TAG, "timer not running: no arcs");
    }
}

ArcModifier::~ArcModifier() {
    if (timer_ != nullptr) {
        esp_timer_stop(timer_);
        esp_timer_delete(timer_);
    }
}

void ArcModifier::OnTap() {
    std::lock_guard<std::mutex> lock(mutex_);
    nb_arc_on_tap(&arc_, NowMs());
}

void ArcModifier::OnTimer(void* arg) {
    static_cast<ArcModifier*>(arg)->Tick();
}

void ArcModifier::Tick() {
    const nb_behavior_state_t state = behavior_.State();
    const EnergyModifier::Snapshot energy = energy_.Read();
    nb_arc_input_t in = {};
    in.resting = state == NB_BEHAVIOR_IDLE;
    in.busy = state == NB_BEHAVIOR_ENGAGED || state == NB_BEHAVIOR_SLEEPING ||
              state == NB_BEHAVIOR_FAULT || energy.sleeping;
    in.petting = petting_;
    in.drowsy = energy.drowsy;
    const MoodModifier::Snapshot mood = mood_.Read();
    in.valence = mood.mood.valence - (mood.temperament.valence + mood.bias.valence);
    in.boredom = energy.boredom;
    nb_arc_frame_t frame;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        nb_arc_update(&arc_, &in, NowMs(), &frame);
    }

    if (frame.started != NB_ARC_NONE) {
        ESP_LOGI(TAG, "%s (valence %.2f, boredom %.2f)", nb_arc_name(frame.started), in.valence,
                 in.boredom);
    }
    if (frame.ended != NB_ARC_NONE) {
        ESP_LOGI(TAG, "%s: done", nb_arc_name(frame.ended));
    }
    if (frame.aborted != NB_ARC_NONE) {
        ESP_LOGI(TAG, "%s: aborted", nb_arc_name(frame.aborted));
    }
    if (frame.push) {
        mood_.OnArc(frame.push_valence, frame.push_arousal, nb_arc_name(frame.kind));
    }

    if (frame.kind != NB_ARC_NONE) {
        // Not transient: the return to IDLE after a tap must not cut a sulk.
        // The arc itself, or conversation, sleep and fault, end it.
        if (frame.kind != shown_kind_ || frame.expression != shown_expression_) {
            nb_proposal_t proposal = {frame.expression, priority::kArc,
                                      frame.remaining_ms + kSafetyMs, false};
            arbiter_.Propose(NB_SOURCE_ARC, proposal);
        }
    } else if (shown_kind_ != NB_ARC_NONE) {
        arbiter_.Withdraw(NB_SOURCE_ARC);
    }
    shown_kind_ = frame.kind;
    shown_expression_ = frame.expression;
    if (display_ != nullptr) {
        display_->SetArc(frame.kind != NB_ARC_NONE, frame.look, frame.eye_open);
    }
}
