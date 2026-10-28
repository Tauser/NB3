#include "link_monitor.h"

#include <esp_log.h>
#include <esp_netif.h>

#include "application.h"

#define TAG "Link"

namespace {

constexpr uint64_t kTickPeriodUs = 1000 * 1000;
constexpr uint32_t kRepeatMs = 20 * 1000;
constexpr uint32_t kReminderMs = 5 * 60 * 1000;
constexpr int kBannerMs = 10000;

uint32_t NowMs() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

bool WifiUp() {
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip = {};
    return netif != nullptr && esp_netif_is_netif_up(netif) &&
           esp_netif_get_ip_info(netif, &ip) == ESP_OK && ip.ip.addr != 0;
}

}  // namespace

LinkMonitor::LinkMonitor(FaceDisplay* display, SoundBank* sounds)
    : display_(display), sounds_(sounds) {
    nb_link_init(&link_, NowMs());
    if (display_ != nullptr) {
        errors_seen_ = display_->ErrorsSeen();
    }
    esp_timer_create_args_t args = {};
    args.callback = OnTimer;
    args.arg = this;
    args.name = "nb_link";
    if (esp_timer_create(&args, &timer_) != ESP_OK ||
        esp_timer_start_periodic(timer_, kTickPeriodUs) != ESP_OK) {
        ESP_LOGE(TAG, "timer not running: offline is never announced");
    }
}

LinkMonitor::~LinkMonitor() {
    if (timer_ != nullptr) {
        esp_timer_stop(timer_);
        esp_timer_delete(timer_);
    }
}

void LinkMonitor::OnTimer(void* arg) {
    static_cast<LinkMonitor*>(arg)->Tick();
}

void LinkMonitor::Announce(nb_link_state_t state) {
    if (display_ == nullptr) {
        return;
    }
    switch (state) {
        case NB_LINK_NO_WIFI:
            display_->ShowNotification("Sem Wi-Fi: toque, lembretes e cabeça continuam", kBannerMs);
            break;
        case NB_LINK_NO_SERVER:
            display_->ShowNotification("Sem conexão com o servidor: o resto continua", kBannerMs);
            break;
        case NB_LINK_ONLINE:
            display_->ShowNotification("Conectado de novo", kBannerMs / 2);
            break;
        default:
            break;
    }
}

void LinkMonitor::Tick() {
    const uint32_t now = NowMs();
    const DeviceState device = Application::GetInstance().GetDeviceState();
    const bool talking = device == kDeviceStateListening || device == kDeviceStateSpeaking;
    const bool server_ok = talking && !was_talking_;  // a conversation got going
    was_talking_ = talking;
    bool server_error = false;
    if (display_ != nullptr) {
        const uint32_t errors = display_->ErrorsSeen();
        server_error = errors != errors_seen_;
        errors_seen_ = errors;
    }
    const nb_link_state_t before = link_.state;
    if (nb_link_update(&link_, WifiUp(), server_ok, server_error, now)) {
        state_ = link_.state;
        ESP_LOGI(TAG, "%s -> %s", nb_link_state_name(before), nb_link_state_name(link_.state));
        if (link_.state == NB_LINK_ONLINE) {
            if (offline_announced_) {
                Announce(NB_LINK_ONLINE);
                if (sounds_ != nullptr) {
                    sounds_->Play(SoundBank::kOnline);
                }
            }
            offline_announced_ = false;
            offline_notices_ = 0;
        } else {
            offline_notices_ = 0;
        }
    }
    // Offline: said now, again a little later (boot notices tend to cover
    // the first one), then every few minutes.
    if (link_.state == NB_LINK_NO_WIFI || link_.state == NB_LINK_NO_SERVER) {
        const uint32_t gap = offline_notices_ < 2 ? kRepeatMs : kReminderMs;
        if (offline_notices_ == 0 || now - last_notice_ms_ >= gap) {
            Announce(link_.state);
            if (offline_notices_ == 0 && sounds_ != nullptr) {
                sounds_->Play(SoundBank::kOffline);  // once: the repeats are silent
            }
            offline_notices_++;
            offline_announced_ = true;
            last_notice_ms_ = now;
        }
    }
}
