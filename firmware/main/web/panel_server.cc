#include "panel_server.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <cJSON.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_random.h>

#include "application.h"
#include "board.h"
#include "core/nb_led_settings.h"
#include "core/nb_panel_files.h"
#include "mcp/device_tools.h"
#include "web/panel_page.h"

#define TAG "Panel"

namespace {

constexpr uint32_t kIdleMs = 5 * 60 * 1000;  // unused for this long: the panel closes
constexpr size_t kQueryMax = 160;
constexpr long kDownloadMax = 130 * 1024;     // a slot file is 128 KB

uint32_t NowMs() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

esp_err_t Reply(httpd_req_t* req, const char* status, const char* text,
                const char* type = "text/plain") {
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, type);
    return httpd_resp_sendstr(req, text);
}

esp_err_t ReplyJson(httpd_req_t* req, cJSON* json) {
    char* text = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (text == nullptr) {
        return Reply(req, "500 Internal Server Error", "out of memory");
    }
    httpd_resp_set_type(req, "application/json");
    const esp_err_t result = httpd_resp_sendstr(req, text);
    cJSON_free(text);
    return result;
}

// A value of the query string; the names the panel accepts have no characters
// that need decoding, so a '%' is simply refused later by the validators.
bool QueryValue(httpd_req_t* req, const char* key, char* out, size_t capacity) {
    char query[kQueryMax];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        return false;
    }
    return httpd_query_key_value(query, key, out, capacity) == ESP_OK;
}

bool QueryInt(httpd_req_t* req, const char* key, int minimum, int maximum, int* out) {
    char text[12];
    if (!QueryValue(req, key, text, sizeof(text)) || text[0] == '\0') {
        return false;
    }
    char* end = nullptr;
    const long value = strtol(text, &end, 10);
    if (*end != '\0' || value < minimum || value > maximum) {
        return false;
    }
    *out = static_cast<int>(value);
    return true;
}

// The body of a request, up to `capacity` bytes, into a buffer of the caller's.
int ReadBody(httpd_req_t* req, char* out, size_t capacity) {
    if (req->content_len == 0 || req->content_len >= capacity) {
        return -1;
    }
    size_t got = 0;
    while (got < req->content_len) {
        const int n = httpd_req_recv(req, out + got, req->content_len - got);
        if (n <= 0) {
            return -1;
        }
        got += static_cast<size_t>(n);
    }
    out[got] = '\0';
    return static_cast<int>(got);
}

bool StationAddress(char* out, size_t capacity) {
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip = {};
    if (netif == nullptr || !esp_netif_is_netif_up(netif) ||
        esp_netif_get_ip_info(netif, &ip) != ESP_OK || ip.ip.addr == 0) {
        return false;
    }
    snprintf(out, capacity, IPSTR, IP2STR(&ip.ip));
    return true;
}

bool ParseHexColor(const char* text, nb_rgb_t* out) {
    if (strlen(text) != 6) {
        return false;
    }
    char* end = nullptr;
    const unsigned long value = strtoul(text, &end, 16);
    if (*end != '\0') {
        return false;
    }
    out->r = static_cast<uint8_t>(value >> 16);
    out->g = static_cast<uint8_t>(value >> 8);
    out->b = static_cast<uint8_t>(value);
    return true;
}

PanelServer* Self(httpd_req_t* req) {
    return static_cast<PanelServer*>(req->user_ctx);
}

}  // namespace

PanelServer::PanelServer(const Parts& parts) : p_(parts) {
    nb_panel_auth_init(&auth_);
    const esp_timer_create_args_t args = {&PanelServer::OnTimer, this, ESP_TIMER_TASK, "nb_panel",
                                          true};
    if (esp_timer_create(&args, &timer_) != ESP_OK) {
        timer_ = nullptr;
        ESP_LOGE(TAG, "timer not created: the panel cannot be opened");
    }
}

PanelServer::~PanelServer() {
    Close();
    if (timer_ != nullptr) {
        esp_timer_delete(timer_);
    }
}

bool PanelServer::IsOpen() {
    std::lock_guard<std::mutex> lock(auth_mutex_);
    return nb_panel_auth_is_open(&auth_, NowMs());
}

PanelServer::OpenResult PanelServer::Open() {
    char address[16];
    if (timer_ == nullptr) {
        return OpenResult::kFailed;
    }
    if (!StationAddress(address, sizeof(address))) {
        return OpenResult::kNoNetwork;
    }
    std::lock_guard<std::mutex> life(life_mutex_);
    if (server_ == nullptr) {
        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.stack_size = 8192;
        config.max_open_sockets = 3;
        config.max_uri_handlers = 10;
        config.lru_purge_enable = true;
        config.recv_wait_timeout = 10;
        config.send_wait_timeout = 10;
        if (httpd_start(&server_, &config) != ESP_OK) {
            server_ = nullptr;
            ESP_LOGE(TAG, "server not started");
            return OpenResult::kFailed;
        }
        const httpd_uri_t routes[] = {
            {"/", HTTP_GET, &PanelServer::Page, this},
            {"/api/login", HTTP_POST, &PanelServer::Login, this},
            {"/api/status", HTTP_GET, &PanelServer::Status, this},
            {"/api/volume", HTTP_POST, &PanelServer::Volume, this},
            {"/api/led", HTTP_POST, &PanelServer::Led, this},
            {"/api/files", HTTP_GET, &PanelServer::ListFiles, this},
            {"/api/file", HTTP_GET, &PanelServer::GetFile, this},
            {"/api/file", HTTP_PUT, &PanelServer::PutFile, this},
            {"/api/file", HTTP_DELETE, &PanelServer::DeleteFile, this},
        };
        for (const httpd_uri_t& route : routes) {
            httpd_register_uri_handler(server_, &route);
        }
    }
    uint32_t code;
    {
        std::lock_guard<std::mutex> lock(auth_mutex_);
        nb_panel_auth_open(&auth_, esp_random(), NowMs(), kIdleMs);
        code = 0;
        for (int i = 0; i < NB_PANEL_CODE_LEN; i++) {
            code = code * 10 + static_cast<uint32_t>(auth_.code[i] - '0');
        }
    }
    snprintf(banner_, sizeof(banner_), "Painel: http://%s  Código: %06lu", address,
             static_cast<unsigned long>(code));
    opened_ms_ = NowMs();
    idle_seen_ = false;
    if (p_.display != nullptr) {
        p_.display->ShowNotification(banner_, kIdleMs);
    }
    esp_timer_start_periodic(timer_, 1000 * 1000);
    ESP_LOGI(TAG, "open at http://%s (code on the screen)", address);
    return OpenResult::kOpened;
}

void PanelServer::Close() {
    std::lock_guard<std::mutex> life(life_mutex_);
    if (server_ == nullptr) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(auth_mutex_);
        nb_panel_auth_close(&auth_);
    }
    if (timer_ != nullptr) {
        esp_timer_stop(timer_);
    }
    // Handlers never take life_mutex_, so stopping the server while holding it
    // cannot wait on one of them.
    httpd_stop(server_);
    server_ = nullptr;
    if (p_.display != nullptr) {
        p_.display->ShowNotification("Painel fechado", 3000);
    }
    ESP_LOGI(TAG, "closed");
}

void PanelServer::OnTimer(void* arg) {
    static_cast<PanelServer*>(arg)->Tick();
}

void PanelServer::Tick() {
    bool close = !IsOpen();
    {
        std::lock_guard<std::mutex> life(life_mutex_);
        // The panel was asked for during a conversation, so it opens while the
        // robot is busy; it closes when a later conversation starts.
        const bool idle = Application::GetInstance().GetDeviceState() == kDeviceStateIdle;
        if (idle) {
            if (!idle_seen_ && p_.display != nullptr) {
                // The conversation cleared the banner: show the code again.
                const uint32_t used = NowMs() - opened_ms_;
                if (used < kIdleMs) {
                    p_.display->ShowNotification(banner_, kIdleMs - used);
                }
            }
            idle_seen_ = true;
        } else if (idle_seen_) {
            close = true;
        }
    }
    if (close) {
        Close();
    }
}

bool PanelServer::Authorized(httpd_req_t* req) {
    char token[NB_PANEL_TOKEN_LEN + 8];
    if (httpd_req_get_hdr_value_str(req, "X-Token", token, sizeof(token)) != ESP_OK) {
        return false;
    }
    std::lock_guard<std::mutex> lock(auth_mutex_);
    return nb_panel_auth_check(&auth_, token, NowMs());
}

#define REQUIRE_LOGIN(self, req)                                                  \
    do {                                                                          \
        if (!(self)->Authorized(req)) {                                           \
            return Reply((req), "401 Unauthorized", "login needed");              \
        }                                                                         \
    } while (0)

esp_err_t PanelServer::Page(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, kPanelPage, HTTPD_RESP_USE_STRLEN);
}

esp_err_t PanelServer::Login(httpd_req_t* req) {
    PanelServer* self = Self(req);
    char body[24];
    if (ReadBody(req, body, sizeof(body)) < 0 || strncmp(body, "code=", 5) != 0) {
        return Reply(req, "400 Bad Request", "send code=NNNNNN");
    }
    char token[NB_PANEL_TOKEN_LEN + 1];
    nb_panel_login_t result;
    {
        std::lock_guard<std::mutex> lock(self->auth_mutex_);
        result = nb_panel_auth_login(&self->auth_, body + 5, esp_random(), esp_random(), NowMs(),
                                     token);
    }
    if (result == NB_PANEL_LOGIN_CLOSED) {
        return Reply(req, "410 Gone", "panel closed");
    }
    if (result == NB_PANEL_LOGIN_WRONG) {
        return Reply(req, "403 Forbidden", "wrong code");
    }
    cJSON* json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "token", token);
    return ReplyJson(req, json);
}

esp_err_t PanelServer::Status(httpd_req_t* req) {
    PanelServer* self = Self(req);
    REQUIRE_LOGIN(self, req);
    Parts& p = self->p_;
    cJSON* json = DeviceStatusJson(p.link, p.energy, p.behavior, p.store, p.last_reset);
    cJSON_AddNumberToObject(json, "volume", Board::GetInstance().GetAudioCodec()->output_volume());
    const nb_led_settings_t led = p.lights.Settings();
    cJSON* leds = cJSON_AddObjectToObject(json, "led");
    cJSON_AddBoolToObject(leds, "on", led.on);
    cJSON_AddBoolToObject(leds, "fixed", led.fixed_color);
    cJSON_AddNumberToObject(leds, "brightness", led.brightness);
    char rgb[8];
    snprintf(rgb, sizeof(rgb), "%02x%02x%02x", led.color.r, led.color.g, led.color.b);
    cJSON_AddStringToObject(leds, "rgb", rgb);
    uint64_t total = 0;
    uint64_t free_bytes = 0;
    if (p.store.Available() && p.store.Capacity(&total, &free_bytes)) {
        cJSON_AddNumberToObject(json, "card_total_mb", static_cast<double>(total >> 20));
        cJSON_AddNumberToObject(json, "card_free_mb", static_cast<double>(free_bytes >> 20));
    }
    return ReplyJson(req, json);
}

esp_err_t PanelServer::Volume(httpd_req_t* req) {
    PanelServer* self = Self(req);
    REQUIRE_LOGIN(self, req);
    int volume;
    if (!QueryInt(req, "v", 0, 100, &volume)) {
        return Reply(req, "400 Bad Request", "v must be 0 to 100");
    }
    Board::GetInstance().GetAudioCodec()->SetOutputVolume(volume);
    return Reply(req, "200 OK", "ok");
}

esp_err_t PanelServer::Led(httpd_req_t* req) {
    PanelServer* self = Self(req);
    REQUIRE_LOGIN(self, req);
    nb_led_settings_t settings = self->p_.lights.Settings();
    int value;
    if (QueryInt(req, "on", 0, 1, &value)) {
        settings.on = value != 0;
    }
    if (QueryInt(req, "b", 0, 255, &value)) {
        settings.brightness = static_cast<uint8_t>(value);  // clamped to the ceiling by Apply
    }
    if (QueryInt(req, "fixed", 0, 1, &value)) {
        settings.fixed_color = value != 0;
    }
    char rgb[8];
    if (QueryValue(req, "rgb", rgb, sizeof(rgb)) && !ParseHexColor(rgb, &settings.color)) {
        return Reply(req, "400 Bad Request", "rgb must be 6 hex digits");
    }
    self->p_.lights.Apply(settings);
    return Reply(req, "200 OK", "ok");
}

esp_err_t PanelServer::ListFiles(httpd_req_t* req) {
    PanelServer* self = Self(req);
    REQUIRE_LOGIN(self, req);
    char dir[16] = "";
    QueryValue(req, "dir", dir, sizeof(dir));
    const bool known = dir[0] == '\0' || strcmp(dir, "sons") == 0 || strcmp(dir, "slot_a") == 0 ||
                       strcmp(dir, "slot_b") == 0 || strcmp(dir, "log") == 0;
    if (!known) {
        return Reply(req, "400 Bad Request", "unknown folder");
    }
    std::vector<StoreService::Entry> entries;
    if (!self->p_.store.Available() || !self->p_.store.ListDir(dir, &entries)) {
        return Reply(req, "503 Service Unavailable", "no memory card");
    }
    cJSON* list = cJSON_CreateArray();
    for (const StoreService::Entry& e : entries) {
        cJSON* item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "name", e.name.c_str());
        cJSON_AddNumberToObject(item, "size", static_cast<double>(e.size));
        cJSON_AddBoolToObject(item, "dir", e.is_dir);
        cJSON_AddItemToArray(list, item);
    }
    return ReplyJson(req, list);
}

esp_err_t PanelServer::GetFile(httpd_req_t* req) {
    PanelServer* self = Self(req);
    REQUIRE_LOGIN(self, req);
    char path[NB_PANEL_PATH_MAX];
    if (!QueryValue(req, "path", path, sizeof(path)) || !nb_panel_allowed(path, NB_PANEL_READ)) {
        return Reply(req, "400 Bad Request", "invalid path");
    }
    std::vector<uint8_t> bytes;
    if (!self->p_.store.Available() || !self->p_.store.ReadBytes(path, &bytes, kDownloadMax)) {
        return Reply(req, "404 Not Found", "not found");
    }
    httpd_resp_set_type(req, "application/octet-stream");
    return httpd_resp_send(req, reinterpret_cast<const char*>(bytes.data()),
                           static_cast<ssize_t>(bytes.size()));
}

esp_err_t PanelServer::PutFile(httpd_req_t* req) {
    PanelServer* self = Self(req);
    REQUIRE_LOGIN(self, req);
    char path[NB_PANEL_PATH_MAX];
    if (!QueryValue(req, "path", path, sizeof(path)) || !nb_panel_allowed(path, NB_PANEL_WRITE)) {
        return Reply(req, "403 Forbidden", "only sons/<name>.ogg can be written");
    }
    const size_t limit = nb_panel_max_write_bytes(path);
    if (req->content_len < 5 || req->content_len > limit) {
        return Reply(req, "413 Payload Too Large", "an OGG file of up to 64 KB");
    }
    if (!self->p_.store.Available()) {
        return Reply(req, "503 Service Unavailable", "no memory card");
    }
    uint8_t* data = static_cast<uint8_t*>(
        heap_caps_malloc(req->content_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (data == nullptr) {
        return Reply(req, "500 Internal Server Error", "out of memory");
    }
    size_t got = 0;
    while (got < req->content_len) {
        const int n = httpd_req_recv(req, reinterpret_cast<char*>(data) + got,
                                     req->content_len - got);
        if (n <= 0) {
            heap_caps_free(data);
            return ESP_FAIL;
        }
        got += static_cast<size_t>(n);
    }
    esp_err_t result;
    if (memcmp(data, "OggS", 4) != 0) {
        result = Reply(req, "415 Unsupported Media Type", "not an OGG file");
    } else if (!self->p_.store.WriteFile(path, data, got)) {
        result = Reply(req, "500 Internal Server Error", "could not write the card");
    } else {
        self->p_.sounds.OnCard(true);  // read the sounds again: this one may replace an alert
        ESP_LOGI(TAG, "wrote %s (%u bytes)", path, static_cast<unsigned>(got));
        result = Reply(req, "200 OK", "ok");
    }
    heap_caps_free(data);
    return result;
}

esp_err_t PanelServer::DeleteFile(httpd_req_t* req) {
    PanelServer* self = Self(req);
    REQUIRE_LOGIN(self, req);
    char path[NB_PANEL_PATH_MAX];
    if (!QueryValue(req, "path", path, sizeof(path)) || !nb_panel_allowed(path, NB_PANEL_DELETE)) {
        return Reply(req, "403 Forbidden", "only sons/<name>.ogg can be deleted");
    }
    if (!self->p_.store.Available() || !self->p_.store.DeleteFile(path)) {
        return Reply(req, "404 Not Found", "not found");
    }
    self->p_.sounds.OnCard(true);
    ESP_LOGI(TAG, "deleted %s", path);
    return Reply(req, "200 OK", "ok");
}
