#include <stddef.h>
#include <stdint.h>
#include "app_types.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "led.h"
#include "led_strip.h"

#define LED_STRIP_GPIO              (4)
#define LED_STRIP_LED_COUNT         (150U)
#define CHESSBOARD_LED_COUNT        (64U)

uint8_t led_intensity = 80; 
uint8_t color_hint_r = 32,  color_hint_g = 24,  color_hint_b = 0;   
uint8_t color_best_r = 0,   color_best_g = 48,  color_best_b = 0;   
uint8_t color_bg_r   = 0,   color_bg_g = 0,   color_bg_b = 2;     

#define YELLOW_RED_VALUE            (color_hint_r)
#define YELLOW_GREEN_VALUE          (color_hint_g)
#define YELLOW_BLUE_VALUE           (color_hint_b)
#define GREEN_RED_VALUE             (color_best_r)
#define GREEN_GREEN_VALUE           (color_best_g)
#define GREEN_BLUE_VALUE            (color_best_b)
#define BACKGROUND_RED_VALUE        (color_bg_r)
#define BACKGROUND_GREEN_VALUE      (color_bg_g)
#define BACKGROUND_BLUE_VALUE       (color_bg_b)
#define RMT_RESOLUTION_HZ           (10000000U)
#define RMT_MEM_BLOCK_SYMBOLS       (64U)

static const char * const TAG = "LED";
static QueueHandle_t ledQueue = NULL;
static led_strip_handle_t ledStrip = NULL;

static uint8_t squareToIndex(const char square[APP_SQUARE_TEXT_LEN], uint32_t * index);
static esp_err_t setBlueBackground(void);
static esp_err_t applyCommand(const led_command_t * command);

void led_atualizar_config(uint8_t intensidade, uint8_t r, uint8_t g, uint8_t b) {
    led_intensity = intensidade; color_best_r = r; color_best_g = g; color_best_b = b;
}

static uint8_t aplicar_intensidade(uint8_t valor_cor) {
    return (uint8_t)((valor_cor * led_intensity) / 100);
}

static esp_err_t setSquareColor(const char square[APP_SQUARE_TEXT_LEN], uint32_t red, uint32_t green, uint32_t blue) {
    uint32_t index; esp_err_t err = ESP_FAIL;
    if (squareToIndex(square, &index)) {
        err = led_strip_set_pixel(ledStrip, index, aplicar_intensidade(red), aplicar_intensidade(green), aplicar_intensidade(blue));
    }
    return err;
}

void led_set_erro(const char* casa_origem, const char* casa_destino) {
    uint32_t idx1, idx2;
    if (squareToIndex(casa_origem, &idx1)) led_strip_set_pixel(ledStrip, idx1, 255, 0, 0);
    if (squareToIndex(casa_destino, &idx2)) led_strip_set_pixel(ledStrip, idx2, 255, 0, 0);
    led_strip_refresh(ledStrip);
}

void led_limpar_erro(void) {
    setBlueBackground();
    led_strip_refresh(ledStrip);
}

esp_err_t ledInit(QueueHandle_t queue)
{
    esp_err_t err;

    led_strip_config_t stripConfig = {
        .strip_gpio_num = LED_STRIP_GPIO,
        .max_leds = LED_STRIP_LED_COUNT,
        .led_model = LED_MODEL_WS2812,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .flags = {
            .invert_out = 0
        }
    };

    led_strip_rmt_config_t rmtConfig = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = RMT_RESOLUTION_HZ,
        .mem_block_symbols = RMT_MEM_BLOCK_SYMBOLS,
        .flags = {
            .with_dma = 0
        }
    };

    ledQueue = queue;

    err = led_strip_new_rmt_device(&stripConfig, &rmtConfig, &ledStrip);

    if (err == ESP_OK)
    {
        err = setBlueBackground();
    }

    if (err == ESP_OK)
    {
        err = led_strip_refresh(ledStrip);
    }

    return err;
}

static uint8_t squareToIndex(const char square[APP_SQUARE_TEXT_LEN], uint32_t * index)
{
    uint8_t valid = 0U;
    uint32_t file;
    uint32_t rank;

    if ((square != NULL) && (index != NULL))
    {
        if ((square[0] >= 'a') && (square[0] <= 'h') &&
            (square[1] >= '1') && (square[1] <= '8'))
        {
            file = (uint32_t)((uint8_t)square[0] - (uint8_t)'a');
            rank = (uint32_t)((uint8_t)square[1] - (uint8_t)'1');
            *index = (rank * 8U) + file;

            if (*index < CHESSBOARD_LED_COUNT)
            {
                valid = 1U;
            }
        }
    }

    return valid;
}

static esp_err_t setSquareColor(const char square[APP_SQUARE_TEXT_LEN],
                                uint32_t red,
                                uint32_t green,
                                uint32_t blue)
{
    uint32_t index;
    esp_err_t err = ESP_ERR_INVALID_ARG;

    if (squareToIndex(square, &index) != 0U)
    {
        err = led_strip_set_pixel(
            ledStrip,
            index,
            aplicar_intensidade((uint8_t)red),
            aplicar_intensidade((uint8_t)green),
            aplicar_intensidade((uint8_t)blue)
        );
    }

    return err;
}

static esp_err_t setBlueBackground(void)
{
    esp_err_t err = ESP_OK;
    uint32_t index;

    for (index = 0U; index < LED_STRIP_LED_COUNT; index++)
    {
        err = led_strip_set_pixel(
            ledStrip,
            index,
            color_bg_r,
            color_bg_g,
            color_bg_b
        );

        if (err != ESP_OK)
        {
            break;
        }
    }

    return err;
}

static esp_err_t applyCommand(const led_command_t * command)
{
    esp_err_t err = ESP_ERR_INVALID_ARG;
    uint32_t i;

    if (command != NULL)
    {
        err = setBlueBackground();

        if ((err == ESP_OK) && (command->clear == 0U))
        {
            for (i = 0U; i < command->legalCount; i++)
            {
                err = setSquareColor(
                    command->legal[i],
                    color_hint_r,
                    color_hint_g,
                    color_hint_b
                );

                if (err != ESP_OK)
                {
                    break;
                }
            }

            if ((err == ESP_OK) && (command->bestValid != 0U))
            {
                err = setSquareColor(
                    command->bestFrom,
                    color_best_r,
                    color_best_g,
                    color_best_b
                );
            }

            if ((err == ESP_OK) && (command->bestValid != 0U))
            {
                err = setSquareColor(
                    command->bestTo,
                    color_best_r,
                    color_best_g,
                    color_best_b
                );
            }
        }

        if (err == ESP_OK)
        {
            err = led_strip_refresh(ledStrip);
        }

        if (err == ESP_OK)
        {
            ESP_LOGI(TAG, "LED command applied, sequence %lu", (unsigned long)command->sequence);
        }
    }

    return err;
}

void ledTask(void * parameters)
{
    led_command_t command;
    BaseType_t status;
    esp_err_t err;

    (void)parameters;

    for (;;)
    {
        status = xQueueReceive(ledQueue, &command, portMAX_DELAY);

        if (status == pdPASS)
        {
            err = applyCommand(&command);

            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "LED command failed: %ld", (long)err);
            }
        }
    }
}
