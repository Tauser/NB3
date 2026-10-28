#include "sound_bank.h"

#include <cstring>
#include <string_view>

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "application.h"
#include "assets/lang_config.h"

#define TAG "Sounds"

namespace {

constexpr long kMaxBytes = 64 * 1024;

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
