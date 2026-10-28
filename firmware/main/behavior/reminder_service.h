#ifndef NB3_REMINDER_SERVICE_H
#define NB3_REMINDER_SERVICE_H

#include <functional>
#include <mutex>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "behavior/expression_arbiter.h"
#include "behavior/head_behavior.h"
#include "behavior/link_monitor.h"
#include "behavior/sound_bank.h"
#include "core/nb_reminder.h"
#include "face/face_display.h"
#include "memory/clock_service.h"

// Timers and reminders (core/nb_reminder, docs/ROADMAP.md stage 5). Its task
// checks them twice a second and keeps them in NVS, so the ones with a known
// time survive a restart. When one fires the robot itself rings, with no
// cloud needed: a sound, a surprised face, the text on the banner and a nod;
// if it is online and not already talking, the text also goes to the AI as
// if the user had said it, so the AI announces it out loud.
class ReminderService {
public:
    ReminderService(ClockService& clock, ExpressionArbiter& arbiter, FaceDisplay* display,
                    HeadBehavior& head, const LinkMonitor& link, SoundBank& sounds);
    ~ReminderService();

    ReminderService(const ReminderService&) = delete;
    ReminderService& operator=(const ReminderService&) = delete;

    // What a tool sees: the reminders, the clock and when.
    struct Access {
        nb_reminders_t& reminders;
        const nb_clock_t& clock;
        uint64_t now_ms;
    };
    // Runs fn under the lock; what it changes is saved right after.
    void Use(const std::function<void(const Access&)>& fn);

private:
    static void TaskEntry(void* arg);
    void Run();
    void Load();
    void Save();
    void Ring(const nb_reminder_fired_t& fired);

    ClockService& clock_;
    ExpressionArbiter& arbiter_;
    FaceDisplay* display_;
    HeadBehavior& head_;
    const LinkMonitor& link_;
    SoundBank& sounds_;
    std::mutex mutex_;
    nb_reminders_t reminders_;
    TaskHandle_t task_ = nullptr;
};

#endif  // NB3_REMINDER_SERVICE_H
