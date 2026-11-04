#include "sound_tools.h"

#include <string>

#include "mcp_server.h"

void RegisterSoundTools(SoundBank& sounds) {
    McpServer::GetInstance().AddTool(
        "self.sound.play",
        "Play a short sound on the robot's speaker. Use it when the user asks for a sound "
        "(\"toca o som de lembrete\", \"faz um barulhinho\") or to punctuate a reaction; do not "
        "use it to speak, and do not repeat it. name is one of: lembrete (the reminder chime), "
        "sem_internet (no connection), conectado (connected again), parada (safety stop), or "
        "the name, without .ogg, of a sound file the user put in the sons folder of the memory "
        "card (letters, digits, _ and - only, up to 32). The robot cannot list the card's files, "
        "so use a card name only if the user said it. It plays at once and returns; two sounds "
        "less than 1.5 s apart are refused. If it says the sound was not found, tell the user "
        "which name you tried.",
        PropertyList({Property("name", kPropertyTypeString)}),
        [&sounds](const PropertyList& properties) -> ReturnValue {
            const std::string name = properties["name"].value<std::string>();
            switch (sounds.PlayNamed(name.c_str())) {
                case SoundBank::NamedResult::kPlayed:
                    return true;
                case SoundBank::NamedResult::kBadName:
                    return std::string("Not played: the name must be 1 to 32 characters, lowercase "
                                       "letters, digits, _ or -, without the extension.");
                case SoundBank::NamedResult::kTooSoon:
                    return std::string("Not played: another sound was played less than 1.5 s ago.");
                case SoundBank::NamedResult::kNoCard:
                    return std::string("Not played: there is no memory card, so only lembrete, "
                                       "sem_internet, conectado and parada can play.");
                case SoundBank::NamedResult::kNotFound:
                    return std::string("Not played: there is no sons/") + name +
                           ".ogg on the memory card.";
                case SoundBank::NamedResult::kNotOgg:
                    return std::string("Not played: sons/") + name +
                           ".ogg is not an OGG Opus file (or is over 64 KB).";
            }
            return false;
        });
}
