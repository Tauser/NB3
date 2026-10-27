#include "initiative_modifier.h"

#include <esp_log.h>
#include <esp_random.h>

#define TAG "Initiative"

namespace {

constexpr uint64_t kTickPeriodUs = 1000 * 1000;

uint32_t NowMs() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

}  // namespace

InitiativeModifier::InitiativeModifier(ExpressionArbiter& arbiter, BehaviorModifier& behavior,
                                       const nb_personality_t& personality)
    : arbiter_(arbiter), behavior_(behavior) {
    nb_initiative_init(&initiative_, personality.initiative, esp_random(), NowMs());
    ESP_LOGI(TAG, "initiative %.2f: first thought after %lu s of quiet, then every ~%lu s",
             personality.initiative,
             static_cast<unsigned long>(nb_initiative_quiet_ms(personality.initiative) / 1000),
             static_cast<unsigned long>(nb_initiative_gap_ms(personality.initiative) / 1000));
    esp_timer_create_args_t args = {};
    args.callback = OnTimer;
    args.arg = this;
    args.name = "nb_initiative";
    if (esp_timer_create(&args, &timer_) != ESP_OK ||
        esp_timer_start_periodic(timer_, kTickPeriodUs) != ESP_OK) {
        ESP_LOGE(TAG, "timer not running: the robot will take no initiative");
    }
}

InitiativeModifier::~InitiativeModifier() {
    if (timer_ != nullptr) {
        esp_timer_stop(timer_);
        esp_timer_delete(timer_);
    }
}

void InitiativeModifier::OnTimer(void* arg) {
    static_cast<InitiativeModifier*>(arg)->Tick();
}

void InitiativeModifier::Tick() {
    const bool at_rest = behavior_.State() == NB_BEHAVIOR_IDLE;
    nb_initiative_set_gap_scale(&initiative_, gap_scale_);
    switch (nb_initiative_update(&initiative_, at_rest, NowMs())) {
        case NB_INITIATIVE_START: {
            ESP_LOGI(TAG, "thinking on its own for %lu ms",
                     static_cast<unsigned long>(initiative_.duration_ms));
            // Transient: back to IDLE clears it too. The deadline ends it even
            // if a stop is missed.
            nb_proposal_t thought = {NB_EXPR_THINKING, priority::kInitiative,
                                     initiative_.duration_ms, true};
            arbiter_.Propose(NB_SOURCE_INITIATIVE, thought);
            break;
        }
        case NB_INITIATIVE_STOP:
            arbiter_.Withdraw(NB_SOURCE_INITIATIVE);
            break;
        default:
            break;
    }
}
