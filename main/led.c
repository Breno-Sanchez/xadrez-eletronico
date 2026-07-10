#include <stdint.h>
#include <string.h>

#include "app_types.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "led.h"
#include "led_map_generated.h"
#include "led_strip.h"

#define LED_STRIP_GPIO              (38)
#define LED_STRIP_LED_COUNT         (150U)
#define RMT_RESOLUTION_HZ           (10000000U)
#define RMT_MEM_BLOCK_SYMBOLS       (64U)
#define BLINK_MS                    (250U)

#define WEAK_BLUE_R                 (0U)
#define WEAK_BLUE_G                 (20U)
#define WEAK_BLUE_B                 (80U)
#define STRONG_BLUE_R               (0U)
#define STRONG_BLUE_G               (36U)
#define STRONG_BLUE_B               (125U)
#define YELLOW_R                    (70U)
#define YELLOW_G                    (58U)
#define YELLOW_B                    (0U)
#define GREEN_R                     (0U)
#define GREEN_G                     (65U)
#define GREEN_B                     (18U)
#define RED_R                       (130U)
#define RED_G                       (0U)
#define RED_B                       (0U)

static const char * const TAG = "LED";

static QueueHandle_t ledQueue = NULL;
static led_strip_handle_t ledStrip = NULL;
static led_command_t currentCommand;
static uint8_t hasCommand = 0U;
static uint8_t blinkPhase = 1U;
static uint8_t rainbowPhase = 0U;
static uint8_t ledIntensity = 35U;
static uint8_t checkFlashFrames = 0U;
static uint8_t checkPreviouslyActive = 0U;

static uint8_t scale(uint8_t value)
{
    return (uint8_t)(((uint32_t)value * (uint32_t)ledIntensity) / 100U);
}

static uint8_t squareToIndex(uint8_t rank, uint8_t file, uint32_t * index)
{
    uint16_t mapped;

    if ((rank >= 8U) || (file >= 8U) || (index == NULL))
    {
        return 0U;
    }

    mapped = ledMapGenerated[rank][file];

    if ((mapped == LED_MAP_INVALID_INDEX) || (mapped >= LED_STRIP_LED_COUNT))
    {
        return 0U;
    }

    *index = mapped;
    return 1U;
}

static esp_err_t setSquareRgb(uint8_t rank, uint8_t file, uint8_t r, uint8_t g, uint8_t b)
{
    uint32_t index;

    if (squareToIndex(rank, file, &index) == 0U)
    {
        return ESP_ERR_INVALID_ARG;
    }

    return led_strip_set_pixel(ledStrip, index, scale(r), scale(g), scale(b));
}

static esp_err_t setSquareText(const char square[APP_SQUARE_TEXT_LEN], uint8_t r, uint8_t g, uint8_t b)
{
    if ((square == NULL) || (square[0] < 'a') || (square[0] > 'h') || (square[1] < '1') || (square[1] > '8'))
    {
        return ESP_ERR_INVALID_ARG;
    }

    return setSquareRgb((uint8_t)(square[1] - '1'), (uint8_t)(square[0] - 'a'), r, g, b);
}

static esp_err_t clearAll(void)
{
    esp_err_t err = ESP_OK;

    if (ledStrip == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

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

static void wheel(uint8_t pos, uint8_t * r, uint8_t * g, uint8_t * b)
{
    if (pos < 85U)
    {
        *r = (uint8_t)(255U - (pos * 3U));
        *g = 0U;
        *b = (uint8_t)(pos * 3U);
    }
    else if (pos < 170U)
    {
        pos = (uint8_t)(pos - 85U);
        *r = 0U;
        *g = (uint8_t)(pos * 3U);
        *b = (uint8_t)(255U - (pos * 3U));
    }
    else
    {
        pos = (uint8_t)(pos - 170U);
        *r = (uint8_t)(pos * 3U);
        *g = (uint8_t)(255U - (pos * 3U));
        *b = 0U;
    }
}

static uint8_t mapHasAny(const uint8_t map[APP_BOARD_RANK_COUNT])
{
    uint8_t any = 0U;

    if (map != NULL)
    {
        for (uint8_t rank = 0U; rank < 8U; rank++)
        {
            if (map[rank] != 0U)
            {
                any = 1U;
                break;
            }
        }
    }

    return any;
}

static void renderRainbow(void)
{
    for (uint8_t rank = 0U; rank < 8U; rank++)
    {
        for (uint8_t file = 0U; file < 8U; file++)
        {
            uint8_t r;
            uint8_t g;
            uint8_t b;
            wheel((uint8_t)(rainbowPhase + (rank * 20U) + (file * 11U)), &r, &g, &b);
            (void)setSquareRgb(rank, file, r, g, b);
        }
    }
}

static void renderMate(void)
{
    uint16_t winnerLow = 0U;
    uint16_t winnerHigh = 0U;
    uint8_t winnerLower;

    for (uint8_t rank = 0U; rank < 8U; rank++)
    {
        uint8_t bits = (currentCommand.winnerWhite != 0U) ? currentCommand.whitePieces[rank] : currentCommand.blackPieces[rank];

        for (uint8_t file = 0U; file < 8U; file++)
        {
            if ((bits & ((uint8_t)(1U << file))) != 0U)
            {
                if (rank < 4U)
                {
                    winnerLow++;
                }
                else
                {
                    winnerHigh++;
                }
            }
        }
    }

    winnerLower = (winnerLow >= winnerHigh) ? 1U : 0U;

    for (uint8_t rank = 0U; rank < 8U; rank++)
    {
        uint8_t winnerHalf = (((rank < 4U) && (winnerLower != 0U)) || ((rank >= 4U) && (winnerLower == 0U))) ? 1U : 0U;

        for (uint8_t file = 0U; file < 8U; file++)
        {
            if (winnerHalf != 0U)
            {
                (void)setSquareRgb(rank, file, GREEN_R, GREEN_G, GREEN_B);
            }
            else
            {
                (void)setSquareRgb(rank, file, RED_R, RED_G, RED_B);
            }
        }
    }
}



static esp_err_t render(void)
{
    esp_err_t err = clearAll();
    uint8_t checkActive;

    if (err != ESP_OK)
    {
        return err;
    }

    if ((hasCommand != 0U) && (currentCommand.rainbowActive != 0U))
    {
        renderRainbow();
        return led_strip_refresh(ledStrip);
    }

    if ((hasCommand != 0U) && (currentCommand.mateActive != 0U))
    {
        renderMate();
        return led_strip_refresh(ledStrip);
    }

    checkActive = mapHasAny(currentCommand.check);

    if ((checkActive != 0U) && (checkPreviouslyActive == 0U))
    {
        checkFlashFrames = 2U;
    }

    checkPreviouslyActive = (checkActive != 0U) ? 1U : 0U;

    for (uint8_t rank = 0U; rank < 8U; rank++)
    {
        for (uint8_t file = 0U; file < 8U; file++)
        {
            uint8_t bit = (uint8_t)(1U << file);

            if ((currentCommand.physical[rank] & bit) != 0U)
            {
                uint8_t ownTurn = 0U;
                if ((currentCommand.sideToMove == LED_SIDE_WHITE) && ((currentCommand.whitePieces[rank] & bit) != 0U)) ownTurn = 1U;
                if ((currentCommand.sideToMove == LED_SIDE_BLACK) && ((currentCommand.blackPieces[rank] & bit) != 0U)) ownTurn = 1U;

                if (ownTurn != 0U) (void)setSquareRgb(rank, file, STRONG_BLUE_R, STRONG_BLUE_G, STRONG_BLUE_B);
                else (void)setSquareRgb(rank, file, WEAK_BLUE_R, WEAK_BLUE_G, WEAK_BLUE_B);
            }
        }
    }

    if ((currentCommand.blinkActive != 0U) && (blinkPhase != 0U))
    {
        (void)setSquareText(currentCommand.blinkSquare, STRONG_BLUE_R, STRONG_BLUE_G, STRONG_BLUE_B);
    }

    for (uint8_t rank = 0U; rank < 8U; rank++)
    {
        for (uint8_t file = 0U; file < 8U; file++)
        {
            uint8_t bit = (uint8_t)(1U << file);
            if ((currentCommand.legal[rank] & bit) != 0U) (void)setSquareRgb(rank, file, YELLOW_R, YELLOW_G, YELLOW_B);
            if ((currentCommand.best[rank] & bit) != 0U) (void)setSquareRgb(rank, file, GREEN_R, GREEN_G, GREEN_B);
            if ((currentCommand.invalid[rank] & bit) != 0U) (void)setSquareRgb(rank, file, RED_R, RED_G, RED_B);
        }
    }

    if (checkFlashFrames != 0U)
    {
        if (checkFlashFrames == 2U)
        {
            for (uint8_t rank = 0U; rank < 8U; rank++)
            {
                for (uint8_t file = 0U; file < 8U; file++)
                {
                    (void)setSquareRgb(rank, file, RED_R, RED_G, RED_B);
                }
            }
        }

        checkFlashFrames--;
    }

    for (uint8_t rank = 0U; rank < 8U; rank++)
    {
        for (uint8_t file = 0U; file < 8U; file++)
        {
            uint8_t bit = (uint8_t)(1U << file);
            if ((currentCommand.check[rank] & bit) != 0U) (void)setSquareRgb(rank, file, RED_R, RED_G, RED_B);
        }
    }

    return led_strip_refresh(ledStrip);
}



void led_atualizar_config(uint8_t intensidade, uint8_t r, uint8_t g, uint8_t b)
{
    (void)r;
    (void)g;
    (void)b;
    ledIntensity = (intensidade > 100U) ? 100U : intensidade;
}

void led_set_erro(const char * casa_origem, const char * casa_destino)
{
    (void)casa_origem;
    (void)casa_destino;
}

void led_limpar_erro(void)
{
}

esp_err_t ledInit(QueueHandle_t queue)
{
    led_strip_config_t stripConfig = {
        .strip_gpio_num = LED_STRIP_GPIO,
        .max_leds = LED_STRIP_LED_COUNT,
        .led_model = LED_MODEL_WS2812,
        .flags = { .invert_out = 0 }
    };
    led_strip_rmt_config_t rmtConfig = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = RMT_RESOLUTION_HZ,
        .mem_block_symbols = RMT_MEM_BLOCK_SYMBOLS,
        .flags = { .with_dma = 0 }
    };
    esp_err_t err;

    ledQueue = queue;
    (void)memset(&currentCommand, 0, sizeof(currentCommand));
    err = led_strip_new_rmt_device(&stripConfig, &rmtConfig, &ledStrip);

    if (err == ESP_OK) err = clearAll();
    if (err == ESP_OK) err = led_strip_refresh(ledStrip);

    ESP_LOGI(TAG, "LED strip GPIO %d, count %u", LED_STRIP_GPIO, (unsigned int)LED_STRIP_LED_COUNT);
    return err;
}

void ledTask(void * parameters)
{
    BaseType_t status;
    led_command_t command;

    (void)parameters;

    for (;;)
    {
        status = xQueueReceive(ledQueue, &command, pdMS_TO_TICKS(BLINK_MS));

        if (status == pdPASS)
        {
            currentCommand = command;
            hasCommand = 1U;
            blinkPhase = 1U;
            (void)render();
        }
        else
        {
            blinkPhase = (blinkPhase == 0U) ? 1U : 0U;
            rainbowPhase = (uint8_t)(rainbowPhase + 9U);

            if ((hasCommand != 0U) &&
                ((currentCommand.blinkActive != 0U) || (currentCommand.rainbowActive != 0U) || (currentCommand.mateActive != 0U) || (checkFlashFrames != 0U)))
            {
                (void)render();
            }
        }
    }
}
