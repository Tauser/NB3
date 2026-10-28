#include "scs_bus.h"

#include <cstring>

#include <esp_log.h>
#include <esp_timer.h>

#define TAG "ScsBus"

namespace {

constexpr int kRxBuffer = 256;
constexpr int kAttempts = 3;
// A reply starts within the servo's return delay (500 us by default) and
// takes well under 1 ms at 1 Mbaud; this leaves room for a slow servo.
constexpr int64_t kReplyTimeoutUs = 10 * 1000;

}  // namespace

ScsBus::ScsBus(uart_port_t port, gpio_num_t tx, gpio_num_t rx, int baud) : port_(port) {
    uart_config_t cfg = {};
    cfg.baud_rate = baud;
    cfg.data_bits = UART_DATA_8_BITS;
    cfg.parity = UART_PARITY_DISABLE;
    cfg.stop_bits = UART_STOP_BITS_1;
    cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    cfg.source_clk = UART_SCLK_DEFAULT;
    if (uart_driver_install(port_, kRxBuffer, 0, 0, nullptr, 0) != ESP_OK) {
        ESP_LOGE(TAG, "uart %d not installed: no servos", port_);
        return;
    }
    if (uart_param_config(port_, &cfg) != ESP_OK ||
        uart_set_pin(port_, tx, rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) {
        ESP_LOGE(TAG, "uart %d not configured: no servos", port_);
        uart_driver_delete(port_);
        return;
    }
    ok_ = true;
    ESP_LOGI(TAG, "uart %d at %d baud, tx %d rx %d", port_, baud, tx, rx);
}

ScsBus::~ScsBus() {
    if (ok_) {
        uart_driver_delete(port_);
    }
}

bool ScsBus::Write(const uint8_t* tx, size_t tx_len) {
    if (!ok_ || tx == nullptr || tx_len == 0) {
        return false;
    }
    uart_flush_input(port_);  // a late reply to an earlier request is not ours
    const int written = uart_write_bytes(port_, tx, tx_len);
    // At least one tick: at 100 Hz a few ms round down to none, which only
    // checks whether the bytes already left.
    return written == static_cast<int>(tx_len) && uart_wait_tx_done(port_, 2) == ESP_OK;
}

bool ScsBus::Send(const uint8_t* tx, size_t tx_len) {
    return Write(tx, tx_len);
}

bool ScsBus::Transact(const uint8_t* tx, size_t tx_len, uint8_t id, nb_scs_response_t* resp) {
    for (int attempt = 0; attempt < kAttempts; attempt++) {
        if (!Write(tx, tx_len)) {
            continue;
        }
        uint8_t rx[2 * NB_SCS_FRAME_MAX];
        size_t len = 0;
        const int64_t deadline = esp_timer_get_time() + kReplyTimeoutUs;
        nb_scs_parse_status_t status = NB_SCS_PARSE_NO_FRAME;
        while (esp_timer_get_time() < deadline && len < sizeof(rx)) {
            // No wait: a blocking read would sit out its whole timeout for
            // a full buffer. The reply comes within a millisecond.
            const int n = uart_read_bytes(port_, rx + len, sizeof(rx) - len, 0);
            if (n > 0) {
                len += static_cast<size_t>(n);
            }
            if (len < tx_len && memcmp(rx, tx, len) == 0) {
                continue;  // an echo still arriving (a short reply is not a prefix of it)
            }
            const size_t skip = nb_scs_echo_len(rx, len, tx, tx_len);
            status = nb_scs_parse_response(rx + skip, len - skip, id, resp, nullptr);
            if (status == NB_SCS_PARSE_OK) {
                return true;
            }
        }
        if (len == 0) {
            timeouts_++;
        } else {
            bad_frames_++;
        }
    }
    return false;
}

bool ScsBus::Ping(uint8_t id) {
    uint8_t tx[NB_SCS_FRAME_MAX];
    const size_t n = nb_scs_build_ping(tx, sizeof(tx), id);
    nb_scs_response_t resp;
    return n > 0 && Transact(tx, n, id, &resp);
}

bool ScsBus::WriteTorque(uint8_t id, bool enable) {
    uint8_t tx[NB_SCS_FRAME_MAX];
    const size_t n = nb_scs_build_torque(tx, sizeof(tx), id, enable);
    nb_scs_response_t resp;
    return n > 0 && Transact(tx, n, id, &resp);
}

bool ScsBus::WriteGoal(const nb_scs_goal_t& goal) {
    uint8_t tx[NB_SCS_FRAME_MAX];
    const size_t n = nb_scs_build_goal(tx, sizeof(tx), &goal);
    nb_scs_response_t resp;
    return n > 0 && Transact(tx, n, goal.id, &resp);
}

bool ScsBus::ReadTorque(uint8_t id, bool* enabled) {
    uint8_t tx[NB_SCS_FRAME_MAX];
    const size_t n = nb_scs_build_read(tx, sizeof(tx), id, NB_SCS_REG_TORQUE_ENABLE, 1);
    nb_scs_response_t resp;
    if (n == 0 || !Transact(tx, n, id, &resp) || resp.param_len != 1) {
        return false;
    }
    *enabled = resp.params[0] != 0;
    return true;
}

bool ScsBus::ReadFeedback(uint8_t id, nb_scs_feedback_t* fb, uint8_t* error) {
    uint8_t tx[NB_SCS_FRAME_MAX];
    const size_t n = nb_scs_build_read_feedback(tx, sizeof(tx), id);
    nb_scs_response_t resp;
    if (n == 0 || !Transact(tx, n, id, &resp)) {
        return false;
    }
    if (error != nullptr) {
        *error = resp.error;
    }
    return nb_scs_decode_feedback(&resp, fb);
}
