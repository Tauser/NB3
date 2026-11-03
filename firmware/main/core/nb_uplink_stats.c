#include "nb_uplink_stats.h"

#include <string.h>

void nb_uplink_stats_start(nb_uplink_stats_t *stats, uint32_t now_ms) {
    if (stats == NULL || stats->active) {
        return;
    }
    memset(stats, 0, sizeof(*stats));
    stats->active = true;
    stats->started_ms = now_ms;
}

void nb_uplink_stats_on_pushed(nb_uplink_stats_t *stats) {
    if (stats != NULL && stats->active) {
        stats->pushed++;
    }
}

void nb_uplink_stats_on_dropped(nb_uplink_stats_t *stats) {
    if (stats != NULL && stats->active) {
        stats->dropped++;
    }
}

void nb_uplink_stats_on_encoded(nb_uplink_stats_t *stats, size_t bytes) {
    if (stats == NULL || !stats->active) {
        return;
    }
    stats->encoded++;
    stats->opus_bytes += (uint32_t)bytes;
    if (bytes <= NB_UPLINK_SMALL_PACKET_BYTES) {
        stats->small_packets++;
    }
}

void nb_uplink_stats_on_send_dropped(nb_uplink_stats_t *stats) {
    if (stats != NULL && stats->active) {
        stats->send_dropped++;
    }
}

void nb_uplink_stats_on_voice(nb_uplink_stats_t *stats, bool voice, uint32_t now_ms) {
    if (stats == NULL || !stats->active || voice == stats->voice) {
        return;
    }
    stats->voice = voice;
    if (voice) {
        stats->voice_since_ms = now_ms;
        stats->voice_runs++;
    } else {
        stats->voice_ms += now_ms - stats->voice_since_ms;
    }
}

bool nb_uplink_stats_finish(nb_uplink_stats_t *stats, uint32_t now_ms, nb_uplink_report_t *report) {
    if (stats == NULL || report == NULL || !stats->active) {
        return false;
    }
    nb_uplink_stats_on_voice(stats, false, now_ms);
    stats->active = false;

    report->duration_ms = now_ms - stats->started_ms;
    report->pushed = stats->pushed;
    report->dropped = stats->dropped;
    report->encoded = stats->encoded;
    // A frame queued before the session can be dropped or encoded during it.
    const uint32_t gone = stats->dropped + stats->encoded;
    report->pending = stats->pushed > gone ? stats->pushed - gone : 0u;
    report->send_dropped = stats->send_dropped;
    report->small_packets = stats->small_packets;
    report->opus_bytes = stats->opus_bytes;
    report->voice_ms = stats->voice_ms;
    report->voice_runs = stats->voice_runs;
    return true;
}

static void put_be32(uint8_t *out, uint32_t value) {
    out[0] = (uint8_t)(value >> 24);
    out[1] = (uint8_t)(value >> 16);
    out[2] = (uint8_t)(value >> 8);
    out[3] = (uint8_t)value;
}

void nb_uplink_tap_header(uint8_t *out, nb_uplink_tap_kind_t kind, uint32_t seq, uint32_t now_ms) {
    if (out == NULL) {
        return;
    }
    memcpy(out, "NBUP", 4);
    out[4] = (uint8_t)kind;
    out[5] = out[6] = out[7] = 0u;
    put_be32(out + 8, seq);
    put_be32(out + 12, now_ms);
}
