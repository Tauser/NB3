#ifndef NB3_SOUND_BANK_H
#define NB3_SOUND_BANK_H

#include <cstdint>
#include <mutex>
#include <vector>

#include "memory/store_service.h"

// The robot's own alert sounds (docs/ROADMAP.md stage 5). Each event has a
// built-in sound; an OGG Opus file of the same name in noise/sons/ on the
// card replaces it (up to 64 KB, the same format as xiaozhi's sounds). The
// files are read when the card shows up, in a task of their own (the card
// listener runs on the store task, which cannot ask itself), and kept in
// memory, so playing never waits for the card. No card, a missing file or
// one that is not OGG: the built-in sound.
class SoundBank {
public:
    enum Event { kReminder = 0, kOffline, kOnline, kSafetyStop, kEventCount };

    explicit SoundBank(StoreService& store);

    SoundBank(const SoundBank&) = delete;
    SoundBank& operator=(const SoundBank&) = delete;

    // From the store listener.
    void OnCard(bool available);
    // Any task.
    void Play(Event event);

    enum class NamedResult { kPlayed, kBadName, kTooSoon, kNoCard, kNotFound, kNotOgg };
    // The sound sons/<name>.ogg: the four alert names play what the bank holds for
    // them (the card file or the built-in), any other name is read from the card
    // now. Any task but the store's.
    NamedResult PlayNamed(const char* name);

private:
    static void LoadTask(void* arg);
    void Load();

    StoreService& store_;
    std::mutex mutex_;  // sounds_
    std::vector<uint8_t> sounds_[kEventCount];
    bool loading_ = false;  // under mutex_
    int64_t last_named_us_ = 0;  // under mutex_
};

#endif  // NB3_SOUND_BANK_H
