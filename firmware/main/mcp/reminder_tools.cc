#include "reminder_tools.h"

#include <climits>
#include <cstdio>
#include <ctime>
#include <string>

#include <cJSON.h>
#include <esp_log.h>

#include "mcp_server.h"

#define TAG "ReminderTools"

namespace {

// Wall time is local time posing as UTC (xiaozhi adds the zone), so a time
// of day is read and shown with gmtime.
std::string LocalText(int64_t wall_s) {
    const time_t t = static_cast<time_t>(wall_s);
    struct tm tm = {};
    gmtime_r(&t, &tm);
    char text[24];
    strftime(text, sizeof(text), "%Y-%m-%d %H:%M", &tm);
    return text;
}

std::string InText(int64_t s) {
    char text[32];
    if (s < 60) {
        snprintf(text, sizeof(text), "%lld s", static_cast<long long>(s < 0 ? 0 : s));
    } else if (s < 3600) {
        snprintf(text, sizeof(text), "%lld min", static_cast<long long>((s + 30) / 60));
    } else {
        snprintf(text, sizeof(text), "%lld h %02lld min", static_cast<long long>(s / 3600),
                 static_cast<long long>((s % 3600) / 60));
    }
    return text;
}

// "HH:MM" (the next one: today, or tomorrow if already past) or
// "YYYY-MM-DD HH:MM", in local time.
bool ParseAt(const std::string& at, int64_t now_s, int64_t* wall_s) {
    int y = 0, mo = 0, d = 0, h = 0, mi = 0;
    char extra = 0;
    if (sscanf(at.c_str(), "%d-%d-%d %d:%d%c", &y, &mo, &d, &h, &mi, &extra) == 5) {
        if (mo < 1 || mo > 12 || d < 1 || d > 31 || h < 0 || h > 23 || mi < 0 || mi > 59) {
            return false;
        }
        // Days from the civil date, so no time zone gets in the way.
        const int64_t yy = y - (mo <= 2);
        const int64_t era = (yy >= 0 ? yy : yy - 399) / 400;
        const int64_t yoe = yy - era * 400;
        const int64_t doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
        const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        const int64_t days = era * 146097 + doe - 719468;
        *wall_s = days * 86400 + h * 3600 + mi * 60;
        return true;
    }
    if (sscanf(at.c_str(), "%d:%d%c", &h, &mi, &extra) == 2) {
        if (h < 0 || h > 23 || mi < 0 || mi > 59) {
            return false;
        }
        const int64_t midnight = now_s - ((now_s % 86400) + 86400) % 86400;
        int64_t t = midnight + h * 3600 + mi * 60;
        if (t <= now_s) {
            t += 86400;
        }
        *wall_s = t;
        return true;
    }
    return false;
}

const char* StatusText(nb_reminder_status_t s) {
    switch (s) {
        case NB_REMINDER_FULL: return "Not set: the robot already keeps 16 reminders; cancel one first.";
        case NB_REMINDER_NO_CLOCK:
            return "Not set: the robot does not know the time yet (no network since it started). "
                   "A timer in minutes works; a time of day does not.";
        case NB_REMINDER_PAST: return "Not set: that time already went by.";
        case NB_REMINDER_TOO_FAR: return "Not set: at most one year ahead.";
        default: return "Not set: give a short text and a time.";
    }
}

}  // namespace

void RegisterReminderTools(ReminderService& reminders) {
    auto& mcp = McpServer::GetInstance();
    mcp.AddTool(
        "self.reminder.set",
        "Set a timer or a reminder that the robot itself keeps and rings, even if the internet "
        "goes down: at the time it beeps, shows the text, makes a face and nods, and if it is "
        "online you will be asked to say it out loud (a message starting with \"Lembrete "
        "agora:\"; then just tell the user). ALWAYS use this when the user asks to be reminded "
        "or for a timer (\"me lembra daqui a 20 minutos de tirar o bolo\", \"põe um timer de 5 "
        "minutos\", \"me lembra às 15h da reunião\") instead of promising to remember. text is "
        "short, in Portuguese, what to remind (\"tirar o bolo do forno\"). Give either "
        "in_minutes (with in_seconds for less than a minute) for a timer, or at for a time of "
        "day in the robot's local time: \"HH:MM\" (the next one) or \"YYYY-MM-DD HH:MM\". Never "
        "say it is set unless this returns an id; reply with when it will ring.",
        PropertyList({Property("text", kPropertyTypeString),
                      Property("in_minutes", kPropertyTypeInteger, 0, 0, 366 * 24 * 60),
                      Property("in_seconds", kPropertyTypeInteger, 0, 0, 3600),
                      Property("at", kPropertyTypeString, std::string(""))}),
        [&reminders](const PropertyList& p) -> ReturnValue {
            const std::string text = p["text"].value<std::string>();
            const std::string at = p["at"].value<std::string>();
            const int64_t seconds = static_cast<int64_t>(p["in_minutes"].value<int>()) * 60 +
                                    p["in_seconds"].value<int>();
            if ((seconds > 0) == !at.empty()) {
                return std::string("Give either in_minutes/in_seconds or at, not both.");
            }
            nb_reminder_status_t status = NB_REMINDER_INVALID;
            uint32_t id = 0;
            int64_t due = 0;
            bool known = false;
            bool bad_at = false;
            reminders.Use([&](const ReminderService::Access& a) {
                int64_t now_s = 0;
                const bool synced = nb_clock_now_s(&a.clock, a.now_ms, &now_s);
                if (at.empty()) {
                    status = nb_reminders_add_in(&a.reminders, static_cast<uint32_t>(seconds),
                                                 text.c_str(), &a.clock, a.now_ms, &id);
                    due = now_s + seconds;
                    known = synced;
                } else if (!synced) {
                    status = NB_REMINDER_NO_CLOCK;
                } else if (!ParseAt(at, now_s, &due)) {
                    bad_at = true;
                } else {
                    status = nb_reminders_add_at(&a.reminders, due, text.c_str(), &a.clock,
                                                 a.now_ms, &id);
                    known = true;
                }
            });
            if (bad_at) {
                return std::string("Not set: at must be \"HH:MM\" or \"YYYY-MM-DD HH:MM\".");
            }
            ESP_LOGI(TAG, "set \"%s\" in %lld s / at '%s': id %lu, status %d", text.c_str(),
                     static_cast<long long>(seconds), at.c_str(), static_cast<unsigned long>(id),
                     status);
            if (status != NB_REMINDER_OK) {
                return std::string(StatusText(status));
            }
            cJSON* o = cJSON_CreateObject();
            cJSON_AddNumberToObject(o, "id", id);
            if (known) {
                cJSON_AddStringToObject(o, "rings_at", LocalText(due).c_str());
            }
            if (at.empty()) {
                cJSON_AddStringToObject(o, "rings_in", InText(seconds).c_str());
                if (!known) {
                    cJSON_AddStringToObject(o, "note",
                                            "the robot does not know the time yet: if it "
                                            "restarts before it does, this timer is lost");
                }
            }
            return o;
        });

    mcp.AddTool(
        "self.reminder.list",
        "List the timers and reminders the robot is keeping, soonest first, with id, text, when "
        "they ring (local time, if known) and how long until then. Use it when the user asks "
        "what reminders there are (\"quais lembretes eu tenho?\", \"quanto falta pro timer?\") "
        "or before cancelling one. An empty list means there are none.",
        PropertyList(), [&reminders](const PropertyList&) -> ReturnValue {
            cJSON* list = cJSON_CreateArray();
            reminders.Use([&](const ReminderService::Access& a) {
                // Soonest first: a few entries, a simple selection.
                bool taken[NB_REMINDER_MAX] = {};
                for (int k = 0; k < NB_REMINDER_MAX; k++) {
                    int pick = -1;
                    int64_t pick_left = 0;
                    for (int i = 0; i < NB_REMINDER_MAX; i++) {
                        const nb_reminder_entry_t& e = a.reminders.entries[i];
                        int64_t left;
                        if (e.id == 0 || taken[i] ||
                            !nb_reminders_remaining_s(&e, &a.clock, a.now_ms, &left)) {
                            continue;
                        }
                        if (pick < 0 || left < pick_left) {
                            pick = i;
                            pick_left = left;
                        }
                    }
                    if (pick < 0) {
                        break;
                    }
                    taken[pick] = true;
                    const nb_reminder_entry_t& e = a.reminders.entries[pick];
                    cJSON* o = cJSON_CreateObject();
                    cJSON_AddNumberToObject(o, "id", e.id);
                    cJSON_AddStringToObject(o, "text", e.text);
                    if (e.has_wall) {
                        cJSON_AddStringToObject(o, "rings_at", LocalText(e.due_wall_s).c_str());
                    }
                    cJSON_AddStringToObject(o, "rings_in", InText(pick_left).c_str());
                    cJSON_AddItemToArray(list, o);
                }
            });
            return list;
        });

    mcp.AddTool(
        "self.reminder.cancel",
        "Cancel a timer or reminder the robot is keeping, by its id (from self.reminder.list or "
        "from when it was set), or all of them with all = true (only when the user clearly asks "
        "to cancel every reminder). Use it when the user changes their mind (\"cancela o "
        "timer\", \"não precisa mais me lembrar\").",
        PropertyList({Property("id", kPropertyTypeInteger, 0, 0, INT_MAX),
                      Property("all", kPropertyTypeBoolean, false)}),
        [&reminders](const PropertyList& p) -> ReturnValue {
            const uint32_t id = static_cast<uint32_t>(p["id"].value<int>());
            const bool all = p["all"].value<bool>();
            if ((id == 0) != all) {
                return std::string("Give either id or all = true.");
            }
            int gone = 0;
            reminders.Use([&](const ReminderService::Access& a) {
                gone = all ? nb_reminders_cancel_all(&a.reminders)
                           : (nb_reminders_cancel(&a.reminders, id) ? 1 : 0);
            });
            ESP_LOGI(TAG, "cancel %lu%s: %d", static_cast<unsigned long>(id), all ? " (all)" : "",
                     gone);
            if (gone == 0) {
                return std::string("Nothing cancelled: no reminder with that id. Check with "
                                   "self.reminder.list.");
            }
            cJSON* o = cJSON_CreateObject();
            cJSON_AddNumberToObject(o, "cancelled", gone);
            return o;
        });
}
