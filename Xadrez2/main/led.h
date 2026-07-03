#ifndef LED_H
#define LED_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

void led_atualizar_config(uint8_t intensidade, uint8_t r, uint8_t g, uint8_t b);
void led_set_erro(const char* casa_origem, const char* casa_destino);
void led_limpar_erro(void);

esp_err_t ledInit(QueueHandle_t queue);
void ledTask(void * parameters);

#endif