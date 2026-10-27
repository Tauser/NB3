#include "energy_modifier.h"

#include <ctime>

#include <esp_log.h>

#define TAG "Energy"

namespace {

constexpr uint64_t kTickPeriodUs = 1000 * 1000;

uint32_t NowMs() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

}  // namespace

EnergyModifier::EnergyModifier(ExpressionArbiter& arbiter, BehaviorModifier& behavior,
                               ClockService& clock, InitiativeModifier& initiative,
                               FaceDisplay* display)
    : arbiter_(arbiter), behavior_(behavior), clock_(clock), initiative_(initiative),
      display_(display) {
    nb_energy_init(&energy_);
    last_ms_ = NowMs();
    esp_timer_create_args_t args = {};
    args.callback = OnTimer;
    args.arg = this;
    args.name = "nb_energy";
    if (esp_timer_create(&args, &timer_) != ESP_OK ||
        esp_timer_start_periodic(timer_, kTickPeriodUs) != ESP_OK) {
        ESP_LOGE(TAG, "timer not running: energy stays at its start");
    }
}

EnergyModifier::~EnergyModifier() {
    if (timer_ != nullptr) {
        esp_timer_stop(timer_);
        esp_timer_delete(timer_);
    }
}

void EnergyModifier::OnTimer(void* arg) {
    static_cast<EnergyModifier*>(arg)->Tick();
}

// Local minute of the day, or -1 while the clock is not synced. The wall
// clock is local time posing as UTC (xiaozhi adds the zone), so gmtime.
int EnergyModifier::MinuteOfDay() {
    nb_clock_t clock;
    uint64_t now = 0;
    clock_.Snapshot(&clock, &now);
    int64_t wall = 0;
    if (!nb_clock_now_s(&clock, now, &wall)) {
        return -1;
    }
    const time_t t = static_cast<time_t>(wall);
    struct tm tm = {};
    gmtime_r(&t, &tm);
    return tm.tm_hour * 60 + tm.tm_min;
}

void EnergyModifier::Tick() {
    const uint32_t now = NowMs();
    const nb_behavior_state_t state = behavior_.State();
    const bool at_rest = state == NB_BEHAVIOR_IDLE || state == NB_BEHAVIOR_SLEEPING;
    const int minute = MinuteOfDay();
    bool drowsy, wants_sleep;
    float arousal, gap, level;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        nb_energy_tick(&energy_, now - last_ms_, at_rest, minute);
        time_known_ = minute >= 0;
        drowsy = nb_energy_drowsy(&energy_);
        wants_sleep = nb_energy_wants_sleep(&energy_);
        arousal = nb_energy_arousal(&energy_);
        gap = nb_energy_initiative_gap(&energy_);
        level = energy_.energy;
    }
    last_ms_ = now;
    initiative_.SetGapScale(gap);

    if (drowsy != drowsy_shown_) {
        drowsy_shown_ = drowsy;
        ESP_LOGI(TAG, "%s (energy %.2f)", drowsy ? "drowsy" : "awake", level);
        if (drowsy) {
            // Not transient: the sleepy face is how it rests while tired.
            nb_proposal_t sleepy = {NB_EXPR_SLEEPY, priority::kEnergy, 0, false};
            arbiter_.Propose(NB_SOURCE_ENERGY, sleepy);
        } else {
            arbiter_.Withdraw(NB_SOURCE_ENERGY);
        }
    }
    if (wants_sleep && state == NB_BEHAVIOR_IDLE) {
        ESP_LOGI(TAG, "falling asleep (energy %.2f)", level);
        behavior_.Sleep();
    }
    const bool sleeping = behavior_.State() == NB_BEHAVIOR_SLEEPING;
    if (sleeping != sleeping_) {
        sleeping_ = sleeping;
        if (!sleeping) {
            ESP_LOGI(TAG, "woke up (energy %.2f)", level);
        }
    }
    if (display_ != nullptr) {
        display_->SetEnergy(arousal, sleeping);
    }
}

EnergyModifier::Snapshot EnergyModifier::Read() {
    std::lock_guard<std::mutex> lock(mutex_);
    return {energy_.energy, energy_.boredom, nb_energy_drowsy(&energy_), sleeping_.load(),
            time_known_};
}
