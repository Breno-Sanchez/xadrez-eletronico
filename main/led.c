#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "app_types.h"
#include "chess_logic.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "led.h"
#include "led_strip.h"

#define LED_STRIP_GPIO              (38)
#define USED_BOARD_LED_COUNT        (64U)
#define SKIP_BETWEEN_FILES         (5U)
#define PHYSICAL_FILE_STRIDE        (13U)
#define LED_STRIP_LED_COUNT         (104U)

#define RMT_RESOLUTION_HZ           (10000000U)
#define RMT_MEM_BLOCK_SYMBOLS       (64U)

#define BLUE_R                      (0U)
#define BLUE_G                      (18U)
#define BLUE_B                      (55U)

#define YELLOW_R                    (90U)
#define YELLOW_G                    (68U)
#define YELLOW_B                    (0U)

#define GREEN_R                     (0U)
#define GREEN_G                     (90U)
#define GREEN_B                     (0U)

#define RED_R                       (100U)
#define RED_G                       (0U)
#define RED_B                       (0U)

#define BLINK_MS                    (300U)

static const char * const TAG = "LED";

static QueueHandle_t ledQueue = NULL;
static led_strip_handle_t ledStrip = NULL;

static uint8_t led_intensity = 80U;
static led_command_t currentCommand;
static uint8_t hasCommand = 0U;
static uint8_t blinkPhase = 1U;

static uint8_t squareToLedIndex(const char square[APP_SQUARE_TEXT_LEN], uint32_t * index);
static esp_err_t clearAll(void);
static esp_err_t setLedSquare(const char square[APP_SQUARE_TEXT_LEN], uint8_t r, uint8_t g, uint8_t b);
static void squareFromLinCol(int lin, int col, char square[APP_SQUARE_TEXT_LEN]);
static esp_err_t render(void);
static void renderCheckX(void);
static void renderMateSides(uint8_t winnerWhite);

void led_atualizar_config(uint8_t intensidade, uint8_t r, uint8_t g, uint8_t b)
{
    (void)r;
    (void)g;
    (void)b;

    if (intensidade > 100U)
    {
        intensidade = 100U;
    }

    led_intensity = intensidade;
}

void led_set_erro(const char* casa_origem, const char* casa_destino)
{
    currentCommand.invalidActive = 1U;

    if (casa_origem != NULL)
    {
        strncpy(currentCommand.invalidFrom, casa_origem, APP_SQUARE_TEXT_LEN - 1U);
        currentCommand.invalidFrom[APP_SQUARE_TEXT_LEN - 1U] = '\0';
    }

    if (casa_destino != NULL)
    {
        strncpy(currentCommand.invalidTo, casa_destino, APP_SQUARE_TEXT_LEN - 1U);
        currentCommand.invalidTo[APP_SQUARE_TEXT_LEN - 1U] = '\0';
    }

    hasCommand = 1U;
    (void)render();
}

void led_limpar_erro(void)
{
    currentCommand.invalidActive = 0U;
    currentCommand.invalidFrom[0] = '\0';
    currentCommand.invalidTo[0] = '\0';
    (void)render();
}

esp_err_t ledInit(QueueHandle_t queue)
{
    esp_err_t err;

    led_strip_config_t stripConfig = {
        .strip_gpio_num = LED_STRIP_GPIO,
        .max_leds = LED_STRIP_LED_COUNT,
        .led_model = LED_MODEL_WS2812,
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

    memset(&currentCommand, 0, sizeof(currentCommand));
    ledQueue = queue;

    err = led_strip_new_rmt_device(&stripConfig, &rmtConfig, &ledStrip);

    if (err == ESP_OK)
    {
        err = clearAll();
    }

    if (err == ESP_OK)
    {
        err = led_strip_refresh(ledStrip);
    }

    ESP_LOGI(TAG, "LED strip GPIO %d, count %u", LED_STRIP_GPIO, (unsigned int)LED_STRIP_LED_COUNT);

    return err;
}

static uint8_t applyIntensity(uint8_t value)
{
    return (uint8_t)(((uint32_t)value * (uint32_t)led_intensity) / 100U);
}

static uint8_t squareToLedIndex(const char square[APP_SQUARE_TEXT_LEN], uint32_t * index)
{
    if ((square == NULL) || (index == NULL))
    {
        return 0U;
    }

    if ((square[0] < 'a') || (square[0] > 'h') ||
        (square[1] < '1') || (square[1] > '8'))
    {
        return 0U;
    }

    uint32_t fileFromH = (uint32_t)('h' - square[0]);
    uint32_t rank0 = (uint32_t)(square[1] - '1');
    uint32_t base = fileFromH * PHYSICAL_FILE_STRIDE;
    uint32_t offset;

    if ((fileFromH % 2U) == 0U)
    {
        offset = rank0;
    }
    else
    {
        offset = 7U - rank0;
    }

    *index = base + offset;

    return (*index < LED_STRIP_LED_COUNT) ? 1U : 0U;
}

static esp_err_t clearAll(void)
{
    esp_err_t err = ESP_OK;

    for (uint32_t i = 0U; i < LED_STRIP_LED_COUNT; i++)
    {
        err = led_strip_set_pixel(ledStrip, i, 0U, 0U, 0U);

        if (err != ESP_OK)
        {
            break;
        }
    }

    return err;
}

static esp_err_t setLedSquare(const char square[APP_SQUARE_TEXT_LEN], uint8_t r, uint8_t g, uint8_t b)
{
    uint32_t index;

    if (squareToLedIndex(square, &index) == 0U)
    {
        return ESP_ERR_INVALID_ARG;
    }

    return led_strip_set_pixel(
        ledStrip,
        index,
        applyIntensity(r),
        applyIntensity(g),
        applyIntensity(b)
    );
}

static void squareFromLinCol(int lin, int col, char square[APP_SQUARE_TEXT_LEN])
{
    square[0] = (char)('a' + col);
    square[1] = (char)('8' - lin);
    square[2] = '\0';
}

static void renderCheckX(void)
{
    const char * diagA[8] = {"a1", "b2", "c3", "d4", "e5", "f6", "g7", "h8"};
    const char * diagB[8] = {"a8", "b7", "c6", "d5", "e4", "f3", "g2", "h1"};

    for (int i = 0; i < 8; i++)
    {
        (void)setLedSquare(diagA[i], RED_R, RED_G, RED_B);
        (void)setLedSquare(diagB[i], RED_R, RED_G, RED_B);
    }
}

static void renderMateRank(char rank, uint8_t r, uint8_t g, uint8_t b)
{
    char square[APP_SQUARE_TEXT_LEN];

    for (char file = 'a'; file <= 'h'; file++)
    {
        square[0] = file;
        square[1] = rank;
        square[2] = '\0';
        (void)setLedSquare(square, r, g, b);
    }
}

static void renderMateSides(uint8_t winnerWhite)
{
    if (blinkPhase == 0U)
    {
        return;
    }

    if (winnerWhite != 0U)
    {
        renderMateRank('1', GREEN_R, GREEN_G, GREEN_B);
        renderMateRank('2', GREEN_R, GREEN_G, GREEN_B);
        renderMateRank('7', RED_R, RED_G, RED_B);
        renderMateRank('8', RED_R, RED_G, RED_B);
    }
    else
    {
        renderMateRank('7', GREEN_R, GREEN_G, GREEN_B);
        renderMateRank('8', GREEN_R, GREEN_G, GREEN_B);
        renderMateRank('1', RED_R, RED_G, RED_B);
        renderMateRank('2', RED_R, RED_G, RED_B);
    }
}

static esp_err_t render(void)
{
    char square[APP_SQUARE_TEXT_LEN];
    esp_err_t err;

    if (ledStrip == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    err = clearAll();

    if (err != ESP_OK)
    {
        return err;
    }

    for (int lin = 0; lin < 8; lin++)
    {
        for (int col = 0; col < 8; col++)
        {
            if (tabuleiro[lin][col].tipo != VAZIO)
            {
                squareFromLinCol(lin, col, square);
                (void)setLedSquare(square, BLUE_R, BLUE_G, BLUE_B);
            }
        }
    }

    if ((hasCommand != 0U) && (currentCommand.helpEnabled != 0U))
    {
        for (uint32_t i = 0U; i < currentCommand.legalCount; i++)
        {
            (void)setLedSquare(currentCommand.legal[i], YELLOW_R, YELLOW_G, YELLOW_B);
        }

        if (currentCommand.bestValid != 0U)
        {
            (void)setLedSquare(currentCommand.bestFrom, GREEN_R, GREEN_G, GREEN_B);
            (void)setLedSquare(currentCommand.bestTo, GREEN_R, GREEN_G, GREEN_B);
        }
    }

    if ((hasCommand != 0U) && (currentCommand.blinkActive != 0U))
    {
        if (blinkPhase != 0U)
        {
            (void)setLedSquare(currentCommand.blinkSquare, BLUE_R, BLUE_G, BLUE_B);
        }
        else
        {
            (void)setLedSquare(currentCommand.blinkSquare, 0U, 0U, 0U);
        }
    }

    if ((hasCommand != 0U) && (currentCommand.checkActive != 0U))
    {
        renderCheckX();
    }

    if ((hasCommand != 0U) && (currentCommand.invalidActive != 0U))
    {
        (void)setLedSquare(currentCommand.invalidFrom, RED_R, RED_G, RED_B);
        (void)setLedSquare(currentCommand.invalidTo, RED_R, RED_G, RED_B);
    }

    if ((hasCommand != 0U) && (currentCommand.mateActive != 0U))
    {
        renderMateSides(currentCommand.winnerWhite);
    }

    return led_strip_refresh(ledStrip);
}

void ledTask(void * parameters)
{
    led_command_t command;
    BaseType_t status;

    (void)parameters;

    for (;;)
    {
        status = xQueueReceive(ledQueue, &command, pdMS_TO_TICKS(BLINK_MS));

        if (status == pdPASS)
        {
            currentCommand = command;
            hasCommand = 1U;
            blinkPhase = 1U;

            if (render() == ESP_OK)
            {
                ESP_LOGI(TAG, "LED command applied, sequence %lu", (unsigned long)command.sequence);
            }
            else
            {
                ESP_LOGE(TAG, "LED command failed");
            }
        }
        else
        {
            blinkPhase = (blinkPhase == 0U) ? 1U : 0U;

            if ((hasCommand != 0U) &&
                ((currentCommand.blinkActive != 0U) || (currentCommand.mateActive != 0U)))
            {
                (void)render();
            }
        }
    }
}
