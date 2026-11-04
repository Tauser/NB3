#include "sound_bank.h"

#include <cstring>
#include <string>
#include <string_view>

#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "application.h"
#include "assets/lang_config.h"

#define TAG "Sounds"

namespace {

constexpr long kMaxBytes = 64 * 1024;
constexpr int64_t kNamedGapUs = 1500 * 1000;  // a model that loops must not machine-gun the speaker
constexpr size_t kNameMax = 32;

struct Entry {
    const char* file;
    std::string_view builtin;
};

const Entry kEntries[SoundBank::kEventCount] = {
    {"sons/lembrete.ogg", Lang::Sounds::OGG_POPUP},
    {"sons/sem_internet.ogg", Lang::Sounds::OGG_EXCLAMATION},
    {"sons/conectado.ogg", Lang::Sounds::OGG_SUCCESS},
    {"sons/parada.ogg", Lang::Sounds::OGG_EXCLAMATION},
};

}  // namespace

SoundBank::SoundBank(StoreService& store) : store_(store) {}

void SoundBank::OnCard(bool available) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!available) {
        for (auto& s : sounds_) {
            s.clear();  // the card is the truth: what comes back is read again
        }
        return;
    }
    if (loading_) {
        return;
    }
    loading_ = true;
    if (xTaskCreate(LoadTask, "nb_sounds", 4096, this, 2, nullptr) != pdPASS) {
        loading_ = false;
        ESP_LOGE(TAG, "loader not created: the built-in sounds play");
    }
}

void SoundBank::LoadTask(void* arg) {
    static_cast<SoundBank*>(arg)->Load();
    vTaskDelete(nullptr);
}

void SoundBank::Load() {
    for (int e = 0; e < kEventCount; e++) {
        std::vector<uint8_t> bytes;
        const bool read = store_.ReadBytes(kEntries[e].file, &bytes, kMaxBytes);
        // An OGG starts with its capture pattern; anything else is not ours to play.
        const bool ogg = read && bytes.size() > 4 && std::memcmp(bytes.data(), "OggS", 4) == 0;
        if (read && !ogg) {
            ESP_LOGW(TAG, "%s is not an OGG file (or over 64 KB): built-in sound", kEntries[e].file);
        } else if (ogg) {
            ESP_LOGI(TAG, "%s: %u bytes", kEntries[e].file, static_cast<unsigned>(bytes.size()));
        }
        std::lock_guard<std::mutex> lock(mutex_);
        sounds_[e] = ogg ? std::move(bytes) : std::vector<uint8_t>();
    }
    std::lock_guard<std::mutex> lock(mutex_);
    loading_ = false;
}

void SoundBank::Play(Event event) {
    if (event < 0 || event >= kEventCount) {
        return;
    }
    // PlaySound splits the file into packets and copies them before it
    // returns, so the lock only has to keep the bytes alive for the call.
    std::lock_guard<std::mutex> lock(mutex_);
    const std::vector<uint8_t>& own = sounds_[event];
    Application::GetInstance().PlaySound(
        own.empty() ? kEntries[event].builtin
                    : std::string_view(reinterpret_cast<const char*>(own.data()), own.size()));
}

SoundBank::NamedResult SoundBank::PlayNamed(const char* name) {
    // Letters, digits, '_' and '-': no path, so the name cannot leave sons/.
    const size_t length = name == nullptr ? 0 : std::strlen(name);
    bool valid = length > 0 && length <= kNameMax;
    for (size_t i = 0; valid && i < length; i++) {
        const char c = name[i];
        valid = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    }
    if (!valid) {
        return NamedResult::kBadName;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const int64_t now = esp_timer_get_time();
        if (last_named_us_ != 0 && now - last_named_us_ < kNamedGapUs) {
            return NamedResult::kTooSoon;
        }
        last_named_us_ = now;
    }
    for (int e = 0; e < kEventCount; e++) {
        const std::string file = std::string("sons/") + name + ".ogg";
        if (file == kEntries[e].file) {
            Play(static_cast<Event>(e));
            return NamedResult::kPlayed;
        }
    }
    if (!store_.Available()) {
        return NamedResult::kNoCard;
    }
    std::vector<uint8_t> bytes;
    if (!store_.ReadBytes((std::string("sons/") + name + ".ogg").c_str(), &bytes, kMaxBytes)) {
        return NamedResult::kNotFound;
    }
    if (bytes.size() <= 4 || std::memcmp(bytes.data(), "OggS", 4) != 0) {
        return NamedResult::kNotOgg;
    }
    Application::GetInstance().PlaySound(
        std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    return NamedResult::kPlayed;
}
