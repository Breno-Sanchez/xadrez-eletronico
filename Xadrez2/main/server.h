#ifndef SERVER_H
#define SERVER_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// Funções públicas expostas para a main.c
esp_err_t serverInit(QueueHandle_t sensorQueue, QueueHandle_t ledQueue);
esp_err_t serverNetworkInit(void);
void serverTask(void * parameters);

#endif // SERVER_H