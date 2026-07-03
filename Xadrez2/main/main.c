#include <stdint.h>
#include <stddef.h>

#include "app_types.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

// Os três módulos principais do sistema
#include "led.h"
#include "sensor.h"
#include "server.h"

#define SENSOR_QUEUE_LEN              (4U)
#define LED_QUEUE_LEN                 (4U)

// Tamanho da memória dedicada a cada tarefa (Stack)
#define SENSOR_TASK_STACK_WORDS       (3072U)
#define SERVER_TASK_STACK_WORDS       (8192U)
#define LED_TASK_STACK_WORDS          (4096U)

// Prioridade das tarefas no processador
#define SENSOR_TASK_PRIORITY          (5U)
#define SERVER_TASK_PRIORITY          (6U)
#define LED_TASK_PRIORITY             (5U)

static const char * const TAG = "MAIN";

// Filas de comunicação entre as tarefas
static QueueHandle_t sensorQueue = NULL;
static QueueHandle_t ledQueue = NULL;

void app_main(void)
{
    esp_err_t err = ESP_OK;
    BaseType_t status;

    // 1. Cria as filas de comunicação
    sensorQueue = xQueueCreate(SENSOR_QUEUE_LEN, sizeof(sensor_event_t));
    ledQueue = xQueueCreate(LED_QUEUE_LEN, sizeof(led_command_t));

    if ((sensorQueue == NULL) || (ledQueue == NULL))
    {
        ESP_LOGE(TAG, "Falha na criacao das Filas (Queues)");
        err = ESP_FAIL;
    }
    else
    {
        // 2. Inicializa os Módulos (Hardware e Servidor)
        err = ledInit(ledQueue);

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
            err = serverNetworkInit();
        }

        // 3. Cria as Tarefas (Tasks) que rodarão em paralelo
        if (err == ESP_OK)
        {
            status = xTaskCreate(
                ledTask,
                "led_task",
                LED_TASK_STACK_WORDS,
                NULL,
                LED_TASK_PRIORITY,
                NULL
            );

            if (status != pdPASS) err = ESP_FAIL;
        }

        if (err == ESP_OK)
        {
            status = xTaskCreate(
                serverTask,
                "server_task",
                SERVER_TASK_STACK_WORDS,
                NULL,
                SERVER_TASK_PRIORITY,
                NULL
            );

            if (status != pdPASS) err = ESP_FAIL;
        }

        if (err == ESP_OK)
        {
            status = xTaskCreate(
                sensorTask,
                "sensor_task",
                SENSOR_TASK_STACK_WORDS,
                NULL,
                SENSOR_TASK_PRIORITY,
                NULL
            );

            if (status != pdPASS) err = ESP_FAIL;
        }

        // 4. Conclusão
        if (err == ESP_OK)
        {
            ESP_LOGI(TAG, "====== SISTEMA INICIADO COM SUCESSO ======");
        }
        else
        {
            ESP_LOGE(TAG, "Falha critica na inicializacao: %ld", (long)err);
        }
    }
}