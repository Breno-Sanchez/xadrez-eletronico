#include <stdint.h>

#include "app_types.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sensor.h"

#define REED_GPIO                  GPIO_NUM_13
#define SENSOR_POLL_MS             (50U)
#define SENSOR_STABLE_TICKS        (4U)

static const char * const TAG = "SENSOR";

static QueueHandle_t sensorQueue = NULL;

static void setSquare(char dst[APP_SQUARE_TEXT_LEN], char file, char rank);
static sensor_piece_state_t readPieceState(void);
static esp_err_t sendSensorEvent(sensor_piece_state_t state, uint32_t sequence);

esp_err_t sensorInit(QueueHandle_t queue)
{
    esp_err_t err;
    gpio_config_t config = {
        .pin_bit_mask = (1ULL << REED_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    sensorQueue = queue;

    if (sensorQueue == NULL)
    {
        err = ESP_ERR_INVALID_ARG;
    }
    else
    {
        err = gpio_config(&config);
    }

    return err;
}

static void setSquare(char dst[APP_SQUARE_TEXT_LEN], char file, char rank)
{
    if (dst != NULL)
    {
        dst[0] = file;
        dst[1] = rank;
        dst[2] = '\0';
    }
}

static sensor_piece_state_t readPieceState(void)
{
    int level;
    sensor_piece_state_t state;

    level = gpio_get_level(REED_GPIO);

    if (level == 0)
    {
        state = SENSOR_STATE_PRESENT;
    }
    else
    {
        state = SENSOR_STATE_LIFTED;
    }

    return state;
}

static esp_err_t sendSensorEvent(sensor_piece_state_t state, uint32_t sequence)
{
    sensor_event_t event;
    BaseType_t status;
    esp_err_t err = ESP_FAIL;

    event.sequence = sequence;
    event.state = state;
    setSquare(event.square, 'g', '1');

    status = xQueueSend(sensorQueue, &event, portMAX_DELAY);

    if (status == pdPASS)
    {
        err = ESP_OK;

        if (state == SENSOR_STATE_LIFTED)
        {
            ESP_LOGI(TAG, "Knight lifted from %s", event.square);
        }
        else
        {
            ESP_LOGI(TAG, "Knight returned to %s", event.square);
        }
    }

    return err;
}

void sensorTask(void * parameters)
{
    sensor_piece_state_t lastRaw;
    sensor_piece_state_t raw;
    sensor_piece_state_t stable;
    uint32_t stableCounter = 0U;
    uint32_t sequence = 1U;

    (void)parameters;

    lastRaw = readPieceState();
    stable = lastRaw;

    (void)sendSensorEvent(stable, sequence);
    sequence++;

    for (;;)
    {
        raw = readPieceState();

        if (raw == lastRaw)
        {
            if (stableCounter < SENSOR_STABLE_TICKS)
            {
                stableCounter++;
            }
        }
        else
        {
            stableCounter = 0U;
            lastRaw = raw;
        }

        if ((stableCounter >= SENSOR_STABLE_TICKS) && (raw != stable))
        {
            stable = raw;
            (void)sendSensorEvent(stable, sequence);
            sequence++;
        }

        vTaskDelay(pdMS_TO_TICKS(SENSOR_POLL_MS));
    }
}
