#ifndef SENSOR_H
#define SENSOR_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

esp_err_t sensorInit(QueueHandle_t queue);
void sensorTask(void * parameters);

#endif