#pragma once

#include <netinet/in.h>
#include <sys/socket.h>

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "core/nb_uplink_stats.h"

// Bench copy of the Opus packets sent to the server, so they can be decoded and
// heard on a PC (tools/audio_capture.py). It goes to the port after the one of
// CONFIG_AUDIO_DEBUG_UDP_SERVER, which carries the PCM from before the AFE.
class UplinkTap {
public:
    UplinkTap();
    ~UplinkTap();
    UplinkTap(const UplinkTap&) = delete;
    UplinkTap& operator=(const UplinkTap&) = delete;

    void Mark(nb_uplink_tap_kind_t kind);
    void Packet(const uint8_t* payload, size_t size);

private:
    void Send(nb_uplink_tap_kind_t kind, const uint8_t* payload, size_t size);

    int socket_ = -1;
    sockaddr_in server_ = {};
    std::atomic<uint32_t> seq_{0};
};
