#include "memory_tools.h"

#include <climits>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <string>

#include <cJSON.h>
#include <esp_log.h>

#include "mcp_server.h"

#define TAG "MemoryTools"

namespace {

constexpr const char* kNoMemory =
    "Memory unavailable: there is no microSD card in the robot (or it failed). Tell the user "
    "you cannot remember things right now; nothing was saved or looked up.";

double Round2(float v) {
    return std::round(static_cast<double>(v) * 100.0) / 100.0;
}

cJSON* Vec(nb_affect_vec_t v) {
    cJSON* o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "valence", Round2(v.valence));
    cJSON_AddNumberToObject(o, "arousal", Round2(v.arousal));
    return o;
}

std::string AgeText(int64_t s) {
    char text[32];
    if (s < 60) {
        return "agora há pouco";
    } else if (s < 3600) {
        snprintf(text, sizeof(text), "há %lld min", static_cast<long long>(s / 60));
    } else if (s < 48 * 3600) {
        snprintf(text, sizeof(text), "há %lld h", static_cast<long long>(s / 3600));
    } else {
        snprintf(text, sizeof(text), "há %lld dias", static_cast<long long>(s / 86400));
    }
    return text;
}

void AddRecord(cJSON* list, const nb_mem_record_t& r, const MemoryService::Access& a) {
    cJSON* o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "id", r.id);
    if (r.kind == NB_MEM_EPISODE) {
        cJSON_AddStringToObject(o, "kind", r.type == NB_EP_NOTE ? "note" : "episode");
        cJSON_AddStringToObject(o, "type", nb_memory_episode_name(r.type));
    } else {
        cJSON_AddStringToObject(o, "kind", nb_memory_kind_name(r.kind));
    }
    if (r.key[0] != '\0') {
        cJSON_AddStringToObject(o, "key", r.key);
    }
    cJSON_AddStringToObject(o, "text", r.text);
    if (r.about != 0) {
        const nb_mem_record_t* about = nb_memory_get(&a.memory, r.about);
        if (about != nullptr) {
            cJSON_AddStringToObject(o, "about", about->text);
        }
    }
    int64_t age = 0;
    if (nb_clock_age_s(&a.clock, &r.when, a.now_ms, &age)) {
        // Wall time is local time posing as UTC (xiaozhi adds the zone).
        const time_t t = static_cast<time_t>(r.when.wall_s);
        struct tm tm = {};
        gmtime_r(&t, &tm);
        char when[24];
        strftime(when, sizeof(when), "%Y-%m-%d %H:%M", &tm);
        cJSON_AddStringToObject(o, "when", when);
        cJSON_AddStringToObject(o, "age", AgeText(age).c_str());
    } else {
        cJSON_AddStringToObject(o, "when", "data desconhecida");
    }
    if (r.kind == NB_MEM_EPISODE) {
        cJSON_AddNumberToObject(o, "feeling", Round2(r.valence));
        cJSON_AddNumberToObject(o, "importance_now",
                                Round2(nb_memory_importance(&r, &a.clock, a.now_ms, nullptr)));
    }
    cJSON_AddItemToArray(list, o);
}

std::string CardError(const char* what) {
    return std::string(what) +
           ": the microSD card failed while writing. Tell the user it may not be kept.";
}

void RegisterMood(MoodModifier& mood, BehaviorModifier& behavior, EnergyModifier& energy) {
    auto& mcp = McpServer::GetInstance();
    mcp.AddTool(
        "self.mood.get",
        "Read how the robot feels right now, from its own body, not from the conversation. "
        "Returns the behavioral state (idle, engaged, reacting, sleeping, fault), the mood as "
        "valence (-1 unhappy .. +1 happy) and arousal (-1 calm/sleepy .. +1 alert/excited), the "
        "expression that mood shows, the temperament it returns to, how much recent memories "
        "shift that resting point, and the latest thing that moved it (e.g. touch:pet = being "
        "petted, xiaozhi:happy = a happy reply, conversa: ... = something reported by you), and "
        "its energy (0 spent .. 1 full, it follows the time of day: low at night), boredom (0 .. "
        "1, it grows when left alone), whether it is drowsy and whether it was asleep. Use "
        "it when the user asks how the robot is or feels (\"tá feliz?\", \"como você está?\"), "
        "and answer in the robot's voice from these values.",
        PropertyList(), [&mood, &behavior, &energy](const PropertyList&) -> ReturnValue {
            const MoodModifier::Snapshot s = mood.Read();
            const EnergyModifier::Snapshot e = energy.Read();
            cJSON* o = cJSON_CreateObject();
            cJSON_AddStringToObject(o, "state", nb_behavior_state_name(behavior.State()));
            cJSON_AddItemToObject(o, "mood", Vec(s.mood));
            cJSON_AddStringToObject(o, "shows", nb_expression_name(s.expression));
            cJSON_AddItemToObject(o, "temperament", Vec(s.temperament));
            cJSON_AddItemToObject(o, "memory_bias", Vec(s.bias));
            cJSON* en = cJSON_AddObjectToObject(o, "energy");
            cJSON_AddNumberToObject(en, "level", Round2(e.energy));
            cJSON_AddNumberToObject(en, "boredom", Round2(e.boredom));
            cJSON_AddBoolToObject(en, "drowsy", e.drowsy);
            cJSON_AddBoolToObject(en, "sleeping", e.sleeping);
            cJSON_AddBoolToObject(en, "time_known", e.time_known);
            if (!s.recent.empty()) {
                cJSON_AddStringToObject(o, "recent", s.recent.c_str());
                cJSON_AddNumberToObject(o, "recent_seconds_ago", s.recent_age_ms / 1000);
            }
            return o;
        });

    mcp.AddTool(
        "self.mood.stimulate",
        "Tell the robot's body that something in the conversation touched it, so its mood "
        "moves the way a person's would: the user praised it or said they missed it (positive "
        "valence), scolded or insulted it (negative valence), told exciting news (positive "
        "arousal), or said something calming (negative arousal). valence and arousal go from "
        "-100 to 100; use 30-60 for ordinary moments and 100 only for strong ones. The effect "
        "is small, capped, fades on its own, and repeating it soon counts less each time, so "
        "call it once per moment, not on every sentence. Do not use it to show a face (use "
        "self.face.set_expression for that). reason is a short note in Portuguese of what "
        "happened, e.g. \"elogiou meus olhos\".",
        PropertyList({Property("valence", kPropertyTypeInteger, -100, 100),
                      Property("arousal", kPropertyTypeInteger, 0, -100, 100),
                      Property("reason", kPropertyTypeString)}),
        [&mood](const PropertyList& p) -> ReturnValue {
            const float v = static_cast<float>(p["valence"].value<int>()) / 100.0f;
            const float a = static_cast<float>(p["arousal"].value<int>()) / 100.0f;
            mood.OnConversationEvent(v, a, p["reason"].value<std::string>());
            const MoodModifier::Snapshot s = mood.Read();
            cJSON* o = cJSON_CreateObject();
            cJSON_AddItemToObject(o, "mood", Vec(s.mood));
            return o;
        });
}

void RegisterMemory(MemoryService& memory) {
    auto& mcp = McpServer::GetInstance();
    mcp.AddTool(
        "self.memory.recall",
        "Search the robot's own long-term memory, kept on its microSD card across restarts. "
        "It is what the robot itself remembers, so prefer it over the conversation history "
        "when asked what the robot remembers. "
        "It holds the profile of its owner (name, nickname, language, birthday...), facts and "
        "preferences it was told, notes of events, and episodes it lived (petting, "
        "conversations) with when they happened and how they felt. Call it before answering "
        "anything about the past or the user (\"você lembra...?\", \"qual meu nome?\", \"o que "
        "eu gosto?\") instead of guessing. query is a few keywords in Portuguese (case and "
        "accents do not matter; words under 3 letters are ignored); leave it empty to list the "
        "most relevant. kind narrows it: any, profile, fact, preference, episode. "
        "max_age_hours > 0 keeps only what happened that recently. Each result has an id "
        "(needed to correct or forget it), its text and when it happened. An empty list means "
        "the robot does not remember: say so.",
        PropertyList({Property("query", kPropertyTypeString, std::string("")),
                      Property("kind", kPropertyTypeString, std::string("any")),
                      Property("max_age_hours", kPropertyTypeInteger, 0, 0, 24 * 365),
                      Property("limit", kPropertyTypeInteger, 5, 1, 10)}),
        [&memory](const PropertyList& p) -> ReturnValue {
            const std::string query = p["query"].value<std::string>();
            const std::string kind_name = p["kind"].value<std::string>();
            nb_mem_kind_t kind = NB_MEM_FACT;
            if (kind_name != "any" && !nb_memory_kind_from_name(kind_name.c_str(), &kind)) {
                return std::string("Unknown kind '") + kind_name +
                       "'. Use any, profile, fact, preference or episode.";
            }
            const nb_mem_query_t q = {query.c_str(), kind_name == "any" ? -1 : static_cast<int>(kind),
                                      static_cast<int64_t>(p["max_age_hours"].value<int>()) * 3600};
            const int limit = p["limit"].value<int>();
            cJSON* list = cJSON_CreateArray();
            const bool ok = memory.Query([&](const MemoryService::Access& a) {
                nb_mem_hit_t hits[10];
                const int n = nb_memory_recall(&a.memory, &q, &a.clock, a.now_ms, hits, limit);
                for (int i = 0; i < n; i++) {
                    AddRecord(list, *hits[i].record, a);
                }
            });
            if (!ok) {
                cJSON_Delete(list);
                return std::string(kNoMemory);
            }
            ESP_LOGI(TAG, "recall \"%s\" (%s): %d", query.c_str(), kind_name.c_str(),
                     cJSON_GetArraySize(list));
            return list;
        });

    mcp.AddTool(
        "self.memory.remember",
        "Save something in the robot's own memory, on its microSD card. This is the only "
        "memory that lives in the robot's body and survives restarts; anything not saved here "
        "is gone for the robot after it restarts, whatever else may remember the "
        "conversation. ALWAYS call it when the user asks the robot to remember anything "
        "(\"lembra que...\", \"guarda isso\", \"não esquece...\") or tells their name or "
        "something about themselves worth keeping (\"meu nome é...\", \"eu gosto de...\"). "
        "Never say it was saved unless this tool returned an id; if it returns an error, tell "
        "the user it was not saved. kind: profile = who the owner is, "
        "with key naming the field (nome, apelido, idioma, aniversario...), and the same key "
        "replaces the old value; preference = a like or dislike, key optional (e.g. musica), "
        "same key replaces; fact = anything else true and lasting; note = an event to recall "
        "later (\"hoje fomos ao parque\"), with valence -100..100 for how it felt. text is "
        "short, in Portuguese, in the third person about the user (\"Tauser gosta de café sem "
        "açúcar\"), at most ~90 characters. Do not save passwords, documents, addresses or "
        "anything the user wants private. Returns the id.",
        PropertyList({Property("kind", kPropertyTypeString),
                      Property("text", kPropertyTypeString),
                      Property("key", kPropertyTypeString, std::string("")),
                      Property("valence", kPropertyTypeInteger, 0, -100, 100)}),
        [&memory](const PropertyList& p) -> ReturnValue {
            const std::string kind_name = p["kind"].value<std::string>();
            const std::string text = p["text"].value<std::string>();
            const std::string key = p["key"].value<std::string>();
            const float valence = static_cast<float>(p["valence"].value<int>()) / 100.0f;
            nb_mem_kind_t kind = NB_MEM_FACT;
            const bool note = kind_name == "note";
            if (!note && (!nb_memory_kind_from_name(kind_name.c_str(), &kind) ||
                          kind == NB_MEM_EPISODE)) {
                return std::string("Unknown kind '") + kind_name +
                       "'. Use profile, fact, preference or note.";
            }
            nb_mem_status_t status = NB_MEM_INVALID;
            uint32_t id = 0;
            const bool ok = memory.Query([&](const MemoryService::Access& a) {
                const nb_stamp_t when = nb_clock_stamp(&a.clock, a.now_ms);
                if (note) {
                    nb_mem_episode_t ep = {NB_EP_NOTE, valence, 0.0f, 0.0f, 0, text.c_str()};
                    status = nb_memory_episode(&a.memory, &a.io, &ep, when, &a.clock, a.now_ms,
                                               &id);
                } else {
                    status = nb_memory_remember(&a.memory, &a.io, kind, key.c_str(), text.c_str(),
                                                when, &id);
                }
            });
            if (!ok) {
                return std::string(kNoMemory);
            }
            ESP_LOGI(TAG, "remember %s \"%s\": id %lu, status %d", kind_name.c_str(), text.c_str(),
                     static_cast<unsigned long>(id), status);
            switch (status) {
                case NB_MEM_OK:
                case NB_MEM_UPDATED: {
                    cJSON* o = cJSON_CreateObject();
                    cJSON_AddNumberToObject(o, "id", id);
                    cJSON_AddBoolToObject(o, "replaced_existing", status == NB_MEM_UPDATED);
                    return o;
                }
                case NB_MEM_FULL:
                    return std::string("Memory full: ask the user what can be forgotten first.");
                case NB_MEM_INVALID:
                    return std::string("Not saved: text is empty, or a profile entry has no key.");
                default:
                    return CardError("Not confirmed");
            }
        });

    mcp.AddTool(
        "self.memory.correct",
        "Fix something the robot remembers wrongly, when the user corrects it (\"não, meu "
        "nome é...\", \"eu não gosto mais disso\"). First find it with self.memory.recall to "
        "get its id, then give the whole corrected text (short, Portuguese, third person). The "
        "old version is replaced on the card and never comes back.",
        PropertyList({Property("id", kPropertyTypeInteger, 1, INT_MAX),
                      Property("text", kPropertyTypeString)}),
        [&memory](const PropertyList& p) -> ReturnValue {
            const uint32_t id = static_cast<uint32_t>(p["id"].value<int>());
            const std::string text = p["text"].value<std::string>();
            nb_mem_status_t status = NB_MEM_INVALID;
            const bool ok = memory.Query([&](const MemoryService::Access& a) {
                status = nb_memory_correct(&a.memory, &a.io, id, text.c_str());
            });
            if (!ok) {
                return std::string(kNoMemory);
            }
            ESP_LOGI(TAG, "correct %lu: status %d", static_cast<unsigned long>(id), status);
            switch (status) {
                case NB_MEM_OK: return true;
                case NB_MEM_NOT_FOUND:
                    return std::string("No memory with that id: look it up with self.memory.recall.");
                case NB_MEM_INVALID: return std::string("Not corrected: the new text is empty.");
                default: return CardError("Correction not confirmed");
            }
        });

    mcp.AddTool(
        "self.memory.forget",
        "Erase memories from the robot's own memory on its microSD, for good. Call it whenever "
        "the user asks to forget something (\"esquece isso\", \"apaga o que eu falei "
        "sobre...\"). Ids are never guessed: ALWAYS call self.memory.recall first with words "
        "from the request, then pass the id of each matching result here, one call per id. If "
        "recall finds nothing, tell the user the robot had nothing saved about it. Or give "
        "category to erase a whole kind: profile, fact, preference, episode, or all "
        "(everything the robot remembers; only when the user clearly asks for that, and "
        "confirm with them first). Success means no copy is left on the card; the reply says "
        "how many of each kind went. Never say something was forgotten unless this tool "
        "reported it. What the cloud assistant itself kept from the conversation is not "
        "affected.",
        PropertyList({Property("id", kPropertyTypeInteger, 0, 0, INT_MAX),
                      Property("category", kPropertyTypeString, std::string(""))}),
        [&memory](const PropertyList& p) -> ReturnValue {
            const uint32_t id = static_cast<uint32_t>(p["id"].value<int>());
            const std::string category = p["category"].value<std::string>();
            nb_mem_kind_t kind = NB_MEM_FACT;
            if ((id == 0) == category.empty()) {
                return std::string("Give either id or category, not both.");
            }
            if (id == 0 && category != "all" && !nb_memory_kind_from_name(category.c_str(), &kind)) {
                return std::string("Unknown category '") + category +
                       "'. Use profile, fact, preference, episode or all.";
            }
            nb_mem_status_t status = NB_MEM_INVALID;
            nb_mem_forgot_t gone = {};
            const bool ok = memory.Query([&](const MemoryService::Access& a) {
                if (id != 0) {
                    status = nb_memory_forget_id(&a.memory, &a.io, id, &gone);
                } else if (category == "all") {
                    status = nb_memory_forget_all(&a.memory, &a.io, &gone);
                } else {
                    status = nb_memory_forget_kind(&a.memory, &a.io, kind, &gone);
                }
            });
            if (!ok) {
                return std::string(kNoMemory);
            }
            ESP_LOGI(TAG, "forget id %lu / '%s': status %d", static_cast<unsigned long>(id),
                     category.c_str(), status);
            if (status == NB_MEM_NOT_FOUND) {
                return std::string(
                    "Nothing matched that id: nothing was forgotten. Call self.memory.recall to "
                    "find the right id; if it finds nothing, tell the user the robot had "
                    "nothing saved about it.");
            }
            if (status != NB_MEM_OK) {
                return CardError("Not confirmed: it will be finished at the next start");
            }
            cJSON* o = cJSON_CreateObject();
            cJSON* counts = cJSON_AddObjectToObject(o, "forgotten");
            for (int k = 0; k < NB_MEM_KIND_COUNT; k++) {
                if (gone.count[k] > 0) {
                    cJSON_AddNumberToObject(counts, nb_memory_kind_name(static_cast<nb_mem_kind_t>(k)),
                                            gone.count[k]);
                }
            }
            return o;
        });
}

}  // namespace

void RegisterMemoryTools(MemoryService& memory, MoodModifier& mood, BehaviorModifier& behavior,
                         EnergyModifier& energy) {
    RegisterMood(mood, behavior, energy);
    RegisterMemory(memory);
}
