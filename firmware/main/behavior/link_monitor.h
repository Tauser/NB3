#ifndef NB3_LINK_MONITOR_H
#define NB3_LINK_MONITOR_H

#include <atomic>
#include <cstdint>

#include <esp_timer.h>

#include "core/nb_link.h"
#include "face/face_display.h"

// Whether the cloud is in reach (core/nb_link, docs/ROADMAP.md stage 5).
// Once a second it checks the Wi-Fi address and what the conversation
// showed (an error banner from xiaozhi, or a conversation that got going).
// Going offline is said on the banner, again at the next check and then
// every few minutes, the way the missing card is; coming back is said once.
// The rest of the robot lives the same either way.
class LinkMonitor {
public:
    explicit LinkMonitor(FaceDisplay* display);
    ~LinkMonitor();

    LinkMonitor(const LinkMonitor&) = delete;
    LinkMonitor& operator=(const LinkMonitor&) = delete;

    // Any task. Starting counts as not online.
    bool Online() const { return state_ == NB_LINK_ONLINE; }
    nb_link_state_t State() const { return static_cast<nb_link_state_t>(state_.load()); }

private:
    static void OnTimer(void* arg);
    void Tick();
    void Announce(nb_link_state_t state);

    FaceDisplay* display_;
    std::atomic<int> state_{NB_LINK_STARTING};
    // esp_timer task only.
    nb_link_t link_;
    uint32_t errors_seen_ = 0;
    bool was_talking_ = false;
    bool offline_announced_ = false;
    int offline_notices_ = 0;
    uint32_t last_notice_ms_ = 0;
    esp_timer_handle_t timer_ = nullptr;
};

#endif  // NB3_LINK_MONITOR_H
