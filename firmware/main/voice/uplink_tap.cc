#include "sdkconfig.h"

#if CONFIG_NB3_UPLINK_TAP

#include "voice/uplink_tap.h"

#include <arpa/inet.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {
constexpr const char* TAG = "UplinkTap";
}  // namespace

UplinkTap::UplinkTap() {
    const std::string address = CONFIG_AUDIO_DEBUG_UDP_SERVER;
    const size_t colon = address.find(':');
    if (colon == std::string::npos) {
        ESP_LOGW(TAG, "Invalid server address: %s, should be IP:PORT", address.c_str());
        return;
    }
    const int port = atoi(address.c_str() + colon + 1) + 1;
    server_.sin_family = AF_INET;
    server_.sin_port = htons(static_cast<uint16_t>(port));
    if (inet_pton(AF_INET, address.substr(0, colon).c_str(), &server_.sin_addr) != 1) {
        ESP_LOGW(TAG, "Invalid server address: %s, should be IP:PORT", address.c_str());
        return;
    }
    socket_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_ < 0) {
        ESP_LOGW(TAG, "Failed to create UDP socket");
        return;
    }
    ESP_LOGI(TAG, "Copying the uplink to %s, port %d", address.substr(0, colon).c_str(), port);
}

UplinkTap::~UplinkTap() {
    if (socket_ >= 0) {
        close(socket_);
    }
}

void UplinkTap::Mark(nb_uplink_tap_kind_t kind) { Send(kind, nullptr, 0); }

void UplinkTap::Packet(const uint8_t* payload, size_t size) {
    Send(NB_UPLINK_TAP_PACKET, payload, size);
}

void UplinkTap::Send(nb_uplink_tap_kind_t kind, const uint8_t* payload, size_t size) {
    if (socket_ < 0) {
        return;
    }
    std::vector<uint8_t> datagram(NB_UPLINK_TAP_HEADER_BYTES + size);
    nb_uplink_tap_header(datagram.data(), kind, seq_.fetch_add(1),
                         static_cast<uint32_t>(esp_timer_get_time() / 1000));
    if (size > 0) {
        memcpy(datagram.data() + NB_UPLINK_TAP_HEADER_BYTES, payload, size);
    }
    sendto(socket_, datagram.data(), datagram.size(), 0, reinterpret_cast<sockaddr*>(&server_),
           sizeof(server_));
}

#endif  // CONFIG_NB3_UPLINK_TAP
