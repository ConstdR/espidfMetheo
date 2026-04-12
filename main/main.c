#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_mac.h"
#include "driver/gpio.h"

#include "wifi.h"
#include "sensor.h"
#include "mqttudp_client.h"

static const char *TAG = "main";

/* ── Настройки из menuconfig (idf.py menuconfig) ────────────── */
#define WIFI_SSID        CONFIG_WIFI_SSID
#define WIFI_PASSWORD    CONFIG_WIFI_PASSWORD
#define LED_GPIO         CONFIG_LED_GPIO
#define SLEEP_MINUTES    CONFIG_SLEEP_MINUTES
#define SLEEP_US         (SLEEP_MINUTES * 60ULL * 1000000ULL)

/* Получить уникальный ID устройства из MAC адреса (как в MicroPython) */
static void get_device_id(char *buf, size_t buf_size)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(buf, buf_size,
             "%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

/* ── Точка входа ────────────────────────────────────────────── */
void app_main(void)
{
    uint32_t causes = esp_sleep_get_wakeup_causes();
    if (causes & BIT(ESP_SLEEP_WAKEUP_TIMER)) {
        ESP_LOGI(TAG, "Wakeup from deep sleep");
    } else {
        ESP_LOGI(TAG, "First boot");
    }

    /* Device ID */
    char device_id[13];  // 6 байт × 2 символа + '\0'
    get_device_id(device_id, sizeof(device_id));
    ESP_LOGI(TAG, "Device ID: %s", device_id);

    /* 0. LED */
    gpio_config_t led_conf = {
        .pin_bit_mask = (1ULL << LED_GPIO),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&led_conf);
    gpio_set_level(LED_GPIO, 0);

    /* 1. Wi-Fi */
    wifi_init_sta(WIFI_SSID, WIFI_PASSWORD);
    wifi_wait_connected();

    /* 2. Сенсор BME280 */
    /* Подавляем ожидаемое сообщение от i2c_bus при первой инициализации */
    esp_log_level_set("i2c.master", ESP_LOG_NONE);
    if (!sensor_init()) {
        ESP_LOGE(TAG, "BME280 init failed! Going to sleep anyway.");
        goto deep_sleep;
    }
    esp_log_level_set("i2c.master", ESP_LOG_ERROR);

    /* Ждём первое измерение в normal mode (standby 0.5ms + время замера) */
    vTaskDelay(pdMS_TO_TICKS(500));

    /* 3. MQTT-UDP */
    mqttudp_client_init();

    /* 4. Одно измерение и отправка */
    {
        sensor_data_t data;
        gpio_set_level(LED_GPIO, 1);
        if (sensor_read(&data)) {
            mqttudp_send_sensor_data(&data, device_id);
        } else {
            ESP_LOGW(TAG, "Sensor read failed");
        }
        gpio_set_level(LED_GPIO, 0);
    }

    vTaskDelay(pdMS_TO_TICKS(200));

deep_sleep:
    ESP_LOGI(TAG, "Going to deep sleep for %d min...", SLEEP_MINUTES);
    esp_sleep_enable_timer_wakeup(SLEEP_US);
    esp_deep_sleep_start();
}
