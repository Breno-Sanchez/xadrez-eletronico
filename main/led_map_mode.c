#include <stdint.h>

#include "driver/usb_serial_jtag.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"

#define LED_STRIP_GPIO              (38)
#define LED_STRIP_LED_COUNT         (150U)
#define RMT_RESOLUTION_HZ           (10000000U)
#define RMT_MEM_BLOCK_SYMBOLS       (64U)

#define MAP_BLUE_R                  (0U)
#define MAP_BLUE_G                  (35U)
#define MAP_BLUE_B                  (100U)

#define FILE_COUNT                  (8U)
#define RANK_COUNT                  (8U)
#define MAP_NO_CHAR                 (-1)

static const char * const TAG = "LED_MAP";
static led_strip_handle_t ledStrip = NULL;

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

    if (err == ESP_OK)
    {
        err = led_strip_refresh(ledStrip);
    }

    return err;
}

static esp_err_t showLed(uint32_t index)
{
    esp_err_t err;

    if ((ledStrip == NULL) || (index >= LED_STRIP_LED_COUNT))
    {
        return ESP_ERR_INVALID_ARG;
    }

    err = clearAll();

    if (err == ESP_OK)
    {
        err = led_strip_set_pixel(ledStrip, index, MAP_BLUE_R, MAP_BLUE_G, MAP_BLUE_B);
    }

    if (err == ESP_OK)
    {
        err = led_strip_refresh(ledStrip);
    }

    return err;
}

static esp_err_t ledMapInit(void)
{
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

    esp_err_t err = led_strip_new_rmt_device(&stripConfig, &rmtConfig, &ledStrip);

    if (err == ESP_OK)
    {
        err = clearAll();
    }

    return err;
}

static esp_err_t usbInputInit(void)
{
    usb_serial_jtag_driver_config_t usbConfig = {
        .tx_buffer_size = 1024,
        .rx_buffer_size = 1024
    };

    esp_err_t err = usb_serial_jtag_driver_install(&usbConfig);

    if (err == ESP_ERR_INVALID_STATE)
    {
        err = ESP_OK;
    }

    return err;
}

static int readCommandChar(void)
{
    uint8_t ch = 0U;
    int received = usb_serial_jtag_read_bytes(&ch, 1U, pdMS_TO_TICKS(20U));

    if (received == 1)
    {
        return (int)ch;
    }

    return MAP_NO_CHAR;
}

static void getSquare(uint32_t squareIndex, char square[3])
{
    uint32_t file = squareIndex / RANK_COUNT;
    uint32_t rank = squareIndex % RANK_COUNT;

    square[0] = (char)('a' + (char)file);
    square[1] = (char)('1' + (char)rank);
    square[2] = '\0';
}

static void printStatus(uint32_t squareIndex, uint32_t ledIndex)
{
    char square[3];

    getSquare(squareIndex, square);

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "============================================================");
    ESP_LOGI(TAG, "Mapping square: %s", square);
    ESP_LOGI(TAG, "Testing LED index: %lu", (unsigned long)ledIndex);
    ESP_LOGI(TAG, "a = accept this LED for this square");
    ESP_LOGI(TAG, "r = reject and test next LED");
    ESP_LOGI(TAG, "b = go back one LED");
    ESP_LOGI(TAG, "q = quit, then press Ctrl+] to close monitor");
    ESP_LOGI(TAG, "============================================================");
}

void app_main(void)
{
    uint32_t squareIndex = 0U;
    uint32_t ledIndex = 0U;
    char square[3];


    esp_err_t err = ledMapInit();

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "LED mapper init failed: %ld", (long)err);
        return;
    }

    err = usbInputInit();

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "USB Serial/JTAG input init failed: %ld", (long)err);
        return;
    }

    ESP_LOGI(TAG, "LED mapper ready");
    ESP_LOGI(TAG, "Use a/r/b/q in this monitor");

    (void)showLed(ledIndex);
    printStatus(squareIndex, ledIndex);

    for (;;)
    {
        int ch = readCommandChar();

        if (ch == MAP_NO_CHAR)
        {
            vTaskDelay(pdMS_TO_TICKS(20U));
            continue;
        }

        if ((ch == '\n') || (ch == '\r'))
        {
            continue;
        }

        if (ch == 'a')
        {
            getSquare(squareIndex, square);
            ESP_LOGI(TAG, "MAP_ACCEPT %s %lu", square, (unsigned long)ledIndex);
        
            squareIndex++;
            ledIndex++;

            if (squareIndex >= (FILE_COUNT * RANK_COUNT))
            {
                ESP_LOGI(TAG, "MAP_DONE");
                            (void)clearAll();
                continue;
            }
        }
        else if (ch == 'r')
        {
            ledIndex++;
            ESP_LOGI(TAG, "MAP_REJECT next LED %lu", (unsigned long)ledIndex);
                }
        else if (ch == 'b')
        {
            if (ledIndex > 0U)
            {
                ledIndex--;
            }

            ESP_LOGI(TAG, "MAP_BACK LED %lu", (unsigned long)ledIndex);
                }
        else if (ch == 'q')
        {
            ESP_LOGI(TAG, "MAP_DONE");
                    (void)clearAll();
            continue;
        }
        else
        {
            ESP_LOGW(TAG, "Invalid input. Use a, r, b, or q.");
                    continue;
        }

        if (ledIndex >= LED_STRIP_LED_COUNT)
        {
            ESP_LOGW(TAG, "MAP_DONE_REACHED_LED_LIMIT");
                    (void)clearAll();
            continue;
        }

        (void)showLed(ledIndex);
        printStatus(squareIndex, ledIndex);
    }
}
