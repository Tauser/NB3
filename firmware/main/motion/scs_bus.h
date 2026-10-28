#ifndef NB3_SCS_BUS_H
#define NB3_SCS_BUS_H

#include <cstddef>
#include <cstdint>

#include <driver/gpio.h>
#include <driver/uart.h>

#include "core/nb_scs.h"

// The servo bus (core/nb_scs on a UART): sends a request, drops the echo the
// half-duplex adapter may loop back, and waits for the reply, retrying a few
// times. Not thread-safe on purpose: the motion task is its only user.
class ScsBus {
public:
    ScsBus(uart_port_t port, gpio_num_t tx, gpio_num_t rx, int baud);
    ~ScsBus();

    ScsBus(const ScsBus&) = delete;
    ScsBus& operator=(const ScsBus&) = delete;

    bool ok() const { return ok_; }

    // Request and reply from `id`. False on timeout or a reply that never
    // parsed; resp->error carries the servo's own fault bits.
    bool Transact(const uint8_t* tx, size_t tx_len, uint8_t id, nb_scs_response_t* resp);
    // A frame nobody answers (sync write, broadcast).
    bool Send(const uint8_t* tx, size_t tx_len);

    bool Ping(uint8_t id);
    bool ReadFeedback(uint8_t id, nb_scs_feedback_t* fb, uint8_t* error);
    // Only after nb_motion_safety_torque_allowed() said yes.
    bool WriteTorque(uint8_t id, bool enable);
    // Only after nb_motion_safety_command() accepted the position.
    bool WriteGoal(const nb_scs_goal_t& goal);
    bool ReadTorque(uint8_t id, bool* enabled);

    uint32_t timeouts() const { return timeouts_; }
    uint32_t bad_frames() const { return bad_frames_; }

private:
    bool Write(const uint8_t* tx, size_t tx_len);

    uart_port_t port_;
    bool ok_ = false;
    uint32_t timeouts_ = 0;
    uint32_t bad_frames_ = 0;
};

#endif  // NB3_SCS_BUS_H
