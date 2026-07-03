#include <stdint.h>
#include <stdbool.h>
#include "app_types.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sensor.h"

#define NUM_LINHAS 8
#define NUM_COLUNAS 8
#define SENSOR_POLL_MS (50U)

static const char * const TAG = "SENSOR";
static QueueHandle_t sensorQueue = NULL;

const gpio_num_t pinosLinha[NUM_LINHAS] = { GPIO_NUM_23, GPIO_NUM_22, GPIO_NUM_21, GPIO_NUM_19, GPIO_NUM_18, GPIO_NUM_5, GPIO_NUM_17, GPIO_NUM_16 };
const gpio_num_t pinosColuna[NUM_COLUNAS] = { GPIO_NUM_32, GPIO_NUM_33, GPIO_NUM_25, GPIO_NUM_26, GPIO_NUM_27, GPIO_NUM_14, GPIO_NUM_12, GPIO_NUM_13 };

static bool sensorAnterior[NUM_LINHAS][NUM_COLUNAS] = {false};
static uint32_t sequencia_global = 1U;

static void setSquare(char dst[APP_SQUARE_TEXT_LEN], char file, char rank) {
    dst[0] = file; dst[1] = rank; dst[2] = '\0';
}

esp_err_t sensorInit(QueueHandle_t queue) {
    sensorQueue = queue;
    if (sensorQueue == NULL) return ESP_ERR_INVALID_ARG;

    for (int i = 0; i < NUM_COLUNAS; i++) {
        gpio_config_t configCol = { .pin_bit_mask = (1ULL << pinosColuna[i]), .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE, .pull_down_en = GPIO_PULLDOWN_DISABLE, .intr_type = GPIO_INTR_DISABLE };
        gpio_config(&configCol);
    }
    for (int i = 0; i < NUM_LINHAS; i++) {
        gpio_config_t configLin = { .pin_bit_mask = (1ULL << pinosLinha[i]), .mode = GPIO_MODE_OUTPUT, .pull_up_en = GPIO_PULLUP_DISABLE, .pull_down_en = GPIO_PULLDOWN_DISABLE, .intr_type = GPIO_INTR_DISABLE };
        gpio_config(&configLin);
        gpio_set_level(pinosLinha[i], 1); 
    }
    ESP_LOGI(TAG, "Matriz de sensores 8x8 inicializada");
    return ESP_OK;
}

void sensorTask(void * parameters) {
    (void)parameters;

    for (int i = 0; i < NUM_LINHAS; i++) {
        gpio_set_level(pinosLinha[i], 0);
        for (int j = 0; j < NUM_COLUNAS; j++) {
            sensorAnterior[i][j] = !gpio_get_level(pinosColuna[j]);
        }
        gpio_set_level(pinosLinha[i], 1);
    }

    for (;;) {
        for (int i = 0; i < NUM_LINHAS; i++) {
            gpio_set_level(pinosLinha[i], 0);
            for (int j = 0; j < NUM_COLUNAS; j++) {
                bool estadoAtual = !gpio_get_level(pinosColuna[j]);
                if (estadoAtual != sensorAnterior[i][j]) {
                    sensorAnterior[i][j] = estadoAtual;
                    sensor_event_t event;
                    event.sequence = sequencia_global++;
                    event.state = estadoAtual ? SENSOR_STATE_PRESENT : SENSOR_STATE_LIFTED;
                    setSquare(event.square, 'a' + j, '8' - i);
                    xQueueSend(sensorQueue, &event, 0);
                }
            }
            gpio_set_level(pinosLinha[i], 1);
        }
        vTaskDelay(pdMS_TO_TICKS(SENSOR_POLL_MS));
    }
}