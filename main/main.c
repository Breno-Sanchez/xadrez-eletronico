#include <stddef.h>
#include <stdint.h>

#include "app_types.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "led.h"
#include "sensor.h"
#include "server.h"

#define SENSOR_QUEUE_LEN            (8U)
#define LED_QUEUE_LEN               (8U)

#define SENSOR_TASK_STACK_BYTES     (3072U)
#define GAME_TASK_STACK_BYTES       (8192U)
#define LED_TASK_STACK_BYTES        (4096U)
#define NETWORK_TASK_STACK_BYTES    (8192U)

#define SENSOR_TASK_PRIORITY        (6U)
#define GAME_TASK_PRIORITY          (5U)
#define LED_TASK_PRIORITY           (4U)
#define NETWORK_TASK_PRIORITY       (3U)

#define APP_TASK_COUNT              (4U)

#if CONFIG_FREERTOS_UNICORE
#define APP_CORE_NETWORK            (0)
#define APP_CORE_WORK               (0)
#else
#define APP_CORE_NETWORK            (0)
#define APP_CORE_WORK               (1)
#endif

typedef struct
{
    const char * name;
    TaskFunction_t entry;
    uint32_t stack_bytes;
    UBaseType_t priority;
    BaseType_t core_id;
    StackType_t * stack;
    StaticTask_t * control_block;
    TaskHandle_t * handle;
} app_task_config_t;

static const char * const TAG = "MAIN";

static QueueHandle_t sensorQueue = NULL;
static QueueHandle_t ledQueue = NULL;

static uint8_t sensorQueueStorage[SENSOR_QUEUE_LEN * sizeof(sensor_event_t)];
static uint8_t ledQueueStorage[LED_QUEUE_LEN * sizeof(led_command_t)];
static StaticQueue_t sensorQueueControlBlock;
static StaticQueue_t ledQueueControlBlock;

#define STACK_WORD_COUNT(bytes_)     (((bytes_) + sizeof(StackType_t) - 1U) / sizeof(StackType_t))

static StackType_t sensorTaskStack[STACK_WORD_COUNT(SENSOR_TASK_STACK_BYTES)];
static StackType_t gameTaskStack[STACK_WORD_COUNT(GAME_TASK_STACK_BYTES)];
static StackType_t ledTaskStack[STACK_WORD_COUNT(LED_TASK_STACK_BYTES)];
static StackType_t networkTaskStack[STACK_WORD_COUNT(NETWORK_TASK_STACK_BYTES)];

static StaticTask_t sensorTaskControlBlock;
static StaticTask_t gameTaskControlBlock;
static StaticTask_t ledTaskControlBlock;
static StaticTask_t networkTaskControlBlock;

static TaskHandle_t sensorTaskHandle = NULL;
static TaskHandle_t gameTaskHandle = NULL;
static TaskHandle_t ledTaskHandle = NULL;
static TaskHandle_t networkTaskHandle = NULL;

static void networkTask(void * parameters);
static esp_err_t createStaticTask(const app_task_config_t * config);

static void networkTask(void * parameters)
{
    esp_err_t err;

    (void)parameters;

    ESP_LOGI(TAG, "Network task starting on core %ld", (long)xPortGetCoreID());

    err = serverNetworkInit();

    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "Network task initialized APSTA mode");
    }
    else
    {
        ESP_LOGE(TAG, "Network task initialization failed: %ld", (long)err);
    }

    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(10000U));
    }
}

static esp_err_t createStaticTask(const app_task_config_t * config)
{
    esp_err_t err = ESP_ERR_INVALID_ARG;

    if ((config != NULL) &&
        (config->name != NULL) &&
        (config->entry != NULL) &&
        (config->stack != NULL) &&
        (config->control_block != NULL) &&
        (config->handle != NULL) &&
        (config->stack_bytes > 0U))
    {
        *(config->handle) = xTaskCreateStaticPinnedToCore(
            config->entry,
            config->name,
            config->stack_bytes,
            NULL,
            config->priority,
            config->stack,
            config->control_block,
            config->core_id
        );

        if (*(config->handle) != NULL)
        {
            ESP_LOGI(
                TAG,
                "Task %s started, priority %u, core %ld, stack %lu bytes",
                config->name,
                (unsigned int)config->priority,
                (long)config->core_id,
                (unsigned long)config->stack_bytes
            );
            err = ESP_OK;
        }
        else
        {
            ESP_LOGE(TAG, "Failed to start task %s", config->name);
            err = ESP_FAIL;
        }
    }

    return err;
}

void app_main(void)
{
    esp_err_t err = ESP_OK;

    static const app_task_config_t tasks[APP_TASK_COUNT] = {
        {
            .name = "network_task",
            .entry = networkTask,
            .stack_bytes = NETWORK_TASK_STACK_BYTES,
            .priority = NETWORK_TASK_PRIORITY,
            .core_id = APP_CORE_NETWORK,
            .stack = networkTaskStack,
            .control_block = &networkTaskControlBlock,
            .handle = &networkTaskHandle
        },
        {
            .name = "led_task",
            .entry = ledTask,
            .stack_bytes = LED_TASK_STACK_BYTES,
            .priority = LED_TASK_PRIORITY,
            .core_id = APP_CORE_WORK,
            .stack = ledTaskStack,
            .control_block = &ledTaskControlBlock,
            .handle = &ledTaskHandle
        },
        {
            .name = "game_task",
            .entry = serverTask,
            .stack_bytes = GAME_TASK_STACK_BYTES,
            .priority = GAME_TASK_PRIORITY,
            .core_id = APP_CORE_WORK,
            .stack = gameTaskStack,
            .control_block = &gameTaskControlBlock,
            .handle = &gameTaskHandle
        },
        {
            .name = "sensor_task",
            .entry = sensorTask,
            .stack_bytes = SENSOR_TASK_STACK_BYTES,
            .priority = SENSOR_TASK_PRIORITY,
            .core_id = APP_CORE_WORK,
            .stack = sensorTaskStack,
            .control_block = &sensorTaskControlBlock,
            .handle = &sensorTaskHandle
        }
    };

    sensorQueue = xQueueCreateStatic(
        SENSOR_QUEUE_LEN,
        sizeof(sensor_event_t),
        sensorQueueStorage,
        &sensorQueueControlBlock
    );

    ledQueue = xQueueCreateStatic(
        LED_QUEUE_LEN,
        sizeof(led_command_t),
        ledQueueStorage,
        &ledQueueControlBlock
    );

    if ((sensorQueue == NULL) || (ledQueue == NULL))
    {
        ESP_LOGE(TAG, "Queue creation failed");
        err = ESP_FAIL;
    }

    if (err == ESP_OK)
    {
        err = ledInit(ledQueue);
    }

    if (err == ESP_OK)
    {
        err = sensorInit(sensorQueue);
    }

    if (err == ESP_OK)
    {
        err = serverInit(sensorQueue, ledQueue);
    }

    if (err == ESP_OK)
    {
        for (uint32_t index = 0U; index < APP_TASK_COUNT; index++)
        {
            err = createStaticTask(&tasks[index]);

            if (err != ESP_OK)
            {
                break;
            }
        }
    }

    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "System started with static pinned FreeRTOS tasks");
    }
    else
    {
        ESP_LOGE(TAG, "Critical initialization failure: %ld", (long)err);
    }
}
