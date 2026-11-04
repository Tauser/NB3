#ifndef NB3_PANEL_SERVER_H
#define NB3_PANEL_SERVER_H

#include <mutex>
#include <string>

#include <esp_http_server.h>
#include <esp_timer.h>

#include "behavior/behavior_modifier.h"
#include "behavior/energy_modifier.h"
#include "behavior/link_monitor.h"
#include "behavior/mood_lights.h"
#include "behavior/sound_bank.h"
#include "core/nb_panel_auth.h"
#include "face/face_display.h"
#include "memory/store_service.h"

// The web panel: volume, body LEDs, the sounds on the card and the robot's
// stats, from a browser on the same network. It exists only on request: Open()
// starts the server and shows a one-time code on the robot's screen, and it
// shuts down when nobody uses it for a while or a conversation begins, so it
// holds no memory or Wi-Fi time while the robot talks. The code and the
// session token are checked by core/nb_panel_auth; what a request may touch
// on the card by core/nb_panel_files.
class PanelServer {
public:
    struct Parts {
        LinkMonitor& link;
        EnergyModifier& energy;
        BehaviorModifier& behavior;
        StoreService& store;
        MoodLights& lights;
        SoundBank& sounds;
        FaceDisplay* display;
        const char* last_reset;
    };

    explicit PanelServer(const Parts& parts);
    ~PanelServer();

    PanelServer(const PanelServer&) = delete;
    PanelServer& operator=(const PanelServer&) = delete;

    enum class OpenResult { kOpened, kNoNetwork, kBusy, kFailed };
    // Any task but the HTTP server's. Opening again gives a new code.
    OpenResult Open();
    void Close();
    bool IsOpen();

private:
    static void OnTimer(void* arg);
    void Tick();

    static esp_err_t Page(httpd_req_t* req);
    static esp_err_t Login(httpd_req_t* req);
    static esp_err_t Status(httpd_req_t* req);
    static esp_err_t Volume(httpd_req_t* req);
    static esp_err_t Led(httpd_req_t* req);
    static esp_err_t ListFiles(httpd_req_t* req);
    static esp_err_t GetFile(httpd_req_t* req);
    static esp_err_t PutFile(httpd_req_t* req);
    static esp_err_t DeleteFile(httpd_req_t* req);
    bool Authorized(httpd_req_t* req);

    Parts p_;
    // life_mutex_ guards starting and stopping (Open, Close, Tick); auth_mutex_ guards
    // auth_ and is the only one a request handler takes, briefly. Stopping the
    // server waits for the handlers, so they must never need life_mutex_.
    std::mutex life_mutex_;
    std::mutex auth_mutex_;
    httpd_handle_t server_ = nullptr;  // under life_mutex_
    nb_panel_auth_t auth_;             // under auth_mutex_
    char banner_[80] = "";             // under life_mutex_: what the screen says while open
    uint32_t opened_ms_ = 0;           // under life_mutex_
    bool idle_seen_ = false;           // under life_mutex_: the robot has been idle since opening
    esp_timer_handle_t timer_ = nullptr;
};

#endif  // NB3_PANEL_SERVER_H
