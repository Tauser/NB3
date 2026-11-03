#ifndef NB_UPLINK_STATS_H
#define NB_UPLINK_STATS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Accounts for every microphone frame of one listening session on its way to
// the server, so a cut sentence can be traced to a frame the device lost, a
// frame the encoder shrank to nothing, or a decision the server took. Pure
// core: time comes in as a parameter and the caller serializes access.

// An Opus packet this small carries no speech: it is what DTX emits for a
// frame the encoder judged silent.
#define NB_UPLINK_SMALL_PACKET_BYTES 8u

typedef struct {
    bool active;
    bool voice;
    uint32_t started_ms;
    uint32_t voice_since_ms;
    uint32_t pushed;        // frames offered to the encode queue
    uint32_t dropped;       // frames the full encode queue threw away
    uint32_t encoded;       // Opus packets produced
    uint32_t send_dropped;  // packets the full send queue threw away
    uint32_t small_packets;
    uint32_t opus_bytes;
    uint32_t voice_ms;
    uint32_t voice_runs;
} nb_uplink_stats_t;

typedef struct {
    uint32_t duration_ms;
    uint32_t pushed;
    uint32_t dropped;
    uint32_t encoded;
    uint32_t pending;  // still queued when the session ended
    uint32_t send_dropped;
    uint32_t small_packets;
    uint32_t opus_bytes;
    uint32_t voice_ms;
    uint32_t voice_runs;
} nb_uplink_report_t;

// Starting while a session is active keeps it: the app may enable voice
// processing again without a new turn.
void nb_uplink_stats_start(nb_uplink_stats_t *stats, uint32_t now_ms);

void nb_uplink_stats_on_pushed(nb_uplink_stats_t *stats);
void nb_uplink_stats_on_dropped(nb_uplink_stats_t *stats);
void nb_uplink_stats_on_encoded(nb_uplink_stats_t *stats, size_t bytes);
void nb_uplink_stats_on_send_dropped(nb_uplink_stats_t *stats);
void nb_uplink_stats_on_voice(nb_uplink_stats_t *stats, bool voice, uint32_t now_ms);

// Ends the session and fills `report`. Returns false, leaving `report` alone,
// when no session was active.
bool nb_uplink_stats_finish(nb_uplink_stats_t *stats, uint32_t now_ms, nb_uplink_report_t *report);

// Header of each datagram of the bench capture of what is sent to the server;
// tools/audio_capture.py reads the same layout. All fields are big-endian.
#define NB_UPLINK_TAP_HEADER_BYTES 16u

typedef enum {
    NB_UPLINK_TAP_PACKET = 0,  // followed by one Opus packet
    NB_UPLINK_TAP_TURN_START = 1,
    NB_UPLINK_TAP_TURN_END = 2,
} nb_uplink_tap_kind_t;

void nb_uplink_tap_header(uint8_t *out, nb_uplink_tap_kind_t kind, uint32_t seq, uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif  // NB_UPLINK_STATS_H
