#include "wifi_stream.h"
#include "lwip/sockets.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "W_STREAM";
static int udp_sock = -1;
static struct sockaddr_in dest_addr;
static stream_stats_t s_stats;
static bool active = false;

esp_err_t wifi_stream_init(stream_mode_t mode, const stream_config_t *config) {
    memset(&s_stats, 0, sizeof(s_stats));
    
    dest_addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(8080);
    
    return ESP_OK;
}

esp_err_t wifi_stream_start(void) {
    udp_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (udp_sock < 0) return ESP_FAIL;

    int bc = 1;
    setsockopt(udp_sock, SOL_SOCKET, SO_BROADCAST, &bc, sizeof(bc));

    active = true;
    ESP_LOGI(TAG, "UDP Broadcaster ready on 8080");
    return ESP_OK;
}

esp_err_t wifi_stream_send_frame(const uint8_t *data, size_t len) {
    if (!active || udp_sock < 0) return ESP_FAIL;

    static uint16_t frame_seq = 0;
    uint8_t packet_buf[1448];
    size_t sent = 0;
    uint8_t fragment_idx = 0;
    
    uint8_t total_fragments = (len + 1024 - 1) / 1024;

    while (sent < len) {
        size_t payload_size = (len - sent > 1024) ? 1024 : (len - sent);
        bool is_last = (sent + payload_size >= len);
        
        packet_buf[0] = (frame_seq >> 8) & 0xFF;
        packet_buf[1] = frame_seq & 0xFF;
        packet_buf[2] = (len >> 8) & 0xFF;
        packet_buf[3] = len & 0xFF;
        packet_buf[4] = is_last ? 1 : 0;
        packet_buf[5] = fragment_idx++;
        packet_buf[6] = total_fragments;
        packet_buf[7] = 0;

        memcpy(packet_buf + 8, data + sent, payload_size);
        
        sendto(udp_sock, packet_buf, payload_size + 8, 0, 
               (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        
        sent += payload_size;
    }
    
    frame_seq++;
    s_stats.frames_sent++;
    return ESP_OK;
}

void wifi_stream_get_stats(stream_stats_t *stats) {
    if (stats) memcpy(stats, &s_stats, sizeof(stream_stats_t));
}