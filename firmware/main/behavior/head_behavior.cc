#include "head_behavior.h"

#include <esp_log.h>

#define TAG "HeadBehavior"

namespace {

constexpr uint64_t kTickPeriodUs = 100 * 1000;

uint32_t NowMs() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

}  // namespace

HeadBehavior::HeadBehavior(MotionService& motion, FaceDisplay* display, BehaviorModifier& behavior,
                           EnergyModifier& energy)
    : motion_(motion), display_(display), behavior_(behavior), energy_(energy) {
    const nb_head_follow_cfg_t cfg = nb_head_follow_default_cfg();
    nb_head_follow_init(&follow_, &cfg);
    esp_timer_create_args_t args = {};
    args.callback = OnTimer;
    args.arg = this;
    args.name = "nb_head";
    if (esp_timer_create(&args, &timer_) != ESP_OK ||
        esp_timer_start_periodic(timer_, kTickPeriodUs) != ESP_OK) {
        ESP_LOGE(TAG, "timer not running: the head moves only when the AI asks");
    }
}

HeadBehavior::~HeadBehavior() {
    if (timer_ != nullptr) {
        esp_timer_stop(timer_);
        esp_timer_delete(timer_);
    }
}

void HeadBehavior::OnAiMove() {
    resting_ = false;
    std::lock_guard<std::mutex> lock(mutex_);
    nb_head_follow_on_ai(&follow_, NowMs());
}

void HeadBehavior::OnAiRelax() {
    resting_ = true;
    std::lock_guard<std::mutex> lock(mutex_);
    nb_head_follow_on_ai(&follow_, NowMs());
}

void HeadBehavior::OnTap() {
    std::lock_guard<std::mutex> lock(mutex_);
    nb_head_follow_on_tap(&follow_, NowMs());
}

void HeadBehavior::OnTimer(void* arg) {
    static_cast<HeadBehavior*>(arg)->Tick();
}

void HeadBehavior::Tick() {
    if (resting_) {
        return;
    }
    const MotionService::Status s = motion_.Read();
    nb_head_follow_input_t in = {};
    in.can_move = s.state != NB_MOTION_SAFETY_STATE_FAULT && s.answering[0] && s.answering[1] &&
                  !s.out_of_range[0] && !s.out_of_range[1];
    const nb_behavior_state_t state = behavior_.State();
    in.mode = state == NB_BEHAVIOR_SLEEPING || energy_.Read().sleeping ? NB_FOLLOW_ASLEEP
              : state == NB_BEHAVIOR_ENGAGED                           ? NB_FOLLOW_TALKING
                                                                       : NB_FOLLOW_AWAKE;
    if (display_ != nullptr) {
        in.face_own = display_->FaceLook(&in.face_yaw, &in.face_pitch);
    }
    in.petting = petting_;
    nb_head_follow_out_t out;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        nb_head_follow_update(&follow_, &in, NowMs(), &out);
    }
    if (!out.move) {
        return;
    }
    const nb_head_geometry_t g = motion_.Geometry();
    nb_head_plan_t plan;
    if (!nb_head_plan(&g, out.kind, out.yaw, out.pitch, out.repeats,
                      s.position[NB_MOTION_SAFETY_AXIS_PAN], s.position[NB_MOTION_SAFETY_AXIS_TILT],
                      &plan)) {
        return;
    }
    // A move of its own is unhurried.
    for (int i = 0; i < plan.count; i++) {
        if (plan.points[i].time_ms < out.min_time_ms) {
            plan.total_ms += out.min_time_ms - plan.points[i].time_ms;
            plan.points[i].time_ms = static_cast<uint16_t>(out.min_time_ms);
        }
    }
    motion_.Play(plan);
}
