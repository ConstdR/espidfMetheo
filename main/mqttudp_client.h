#pragma once
#include "sensor.h"

void mqttudp_client_init(void);

/**
 * Отправить данные сенсора как MQTT PUBLISH на топик weather/<device_id>
 * Payload: JSON {"id":"...","temperature":...,"humidity":...,"pressure":...}
 */
void mqttudp_send_sensor_data(const sensor_data_t *data, const char *device_id);
