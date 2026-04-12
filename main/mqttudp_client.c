#include "mqttudp_client.h"

#include "esp_log.h"
#include "esp_rtc_time.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include <string.h>
#include <stdio.h>
#include <errno.h>

static const char *TAG = "mqttudp";

/* ── Настройки ───────────────────────────────────────────────── */
#define MQTTUDP_PORT       1885
#define MQTTUDP_BROADCAST  "255.255.255.255"

/*
 * MQTT PUBLISH пакет (QoS 0):
 *  Byte 0    : 0x30  — PUBLISH, QoS 0
 *  Byte 1    : remaining length (< 128)
 *  Bytes 2-3 : длина topic (big-endian)
 *  Bytes 4.. : topic string
 *  Bytes ..  : payload
 */
static int build_mqtt_publish(uint8_t *buf, size_t buf_size,
                               const char *topic, const char *payload)
{
    size_t topic_len   = strlen(topic);
    size_t payload_len = strlen(payload);
    size_t remain_len  = 2 + topic_len + payload_len;

    if (remain_len >= 128 || (2 + 1 + remain_len) > buf_size) {
        return -1;
    }

    size_t i = 0;
    buf[i++] = 0x30;
    buf[i++] = (uint8_t)remain_len;
    buf[i++] = (uint8_t)((topic_len >> 8) & 0xFF);
    buf[i++] = (uint8_t)(topic_len & 0xFF);
    memcpy(&buf[i], topic,   topic_len);   i += topic_len;
    memcpy(&buf[i], payload, payload_len); i += payload_len;

    return (int)i;
}

/* ── Состояние ───────────────────────────────────────────────── */
static int               sock = -1;
static struct sockaddr_in dest_addr;

/* ── Публичные функции ───────────────────────────────────────── */
void mqttudp_client_init(void)
{
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "socket() failed: errno=%d", errno);
        return;
    }

    int broadcast = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));

    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family      = AF_INET;
    dest_addr.sin_port        = htons(MQTTUDP_PORT);
    dest_addr.sin_addr.s_addr = inet_addr(MQTTUDP_BROADCAST);

    ESP_LOGI(TAG, "MQTT-UDP ready → %s:%d", MQTTUDP_BROADCAST, MQTTUDP_PORT);
}

static void publish(const char *topic, const char *payload)
{
    if (sock < 0) {
        ESP_LOGE(TAG, "Socket not initialized");
        return;
    }

    uint8_t buf[256];
    int len = build_mqtt_publish(buf, sizeof(buf), topic, payload);
    if (len < 0) {
        ESP_LOGE(TAG, "Packet too large");
        return;
    }

    int sent = sendto(sock, buf, (size_t)len, 0,
                      (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (sent < 0) {
        ESP_LOGE(TAG, "sendto failed: errno=%d", errno);
    } else {
        ESP_LOGI(TAG, "→ %s : %s", topic, payload);
    }
}

void mqttudp_send_sensor_data(const sensor_data_t *data, const char *device_id)
{
    /* Топик фиксированный — id устройства уже в payload */
    const char *topic = "weather";

    /* Payload — JSON с данными и timestamp (мс с момента загрузки) */
    char payload[160];
    snprintf(payload, sizeof(payload),
        "{\"id\":\"%s\",\"temperature\":%.1f,\"humidity\":%.1f,\"pressure\":%.1f,\"ts\":%llu}",
        device_id,
        data->temperature,
        data->humidity,
        data->pressure,
        (unsigned long long)(esp_rtc_get_time_us() / 1000ULL));  // мкс → мс

    publish(topic, payload);
}
