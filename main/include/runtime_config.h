#ifndef RUNTIME_CONFIG_H
#define RUNTIME_CONFIG_H

#include <stdint.h>

#include "esp_err.h"
#include "project_config.h"

typedef enum
{
    RUNTIME_LED_COLOR_EMPTY = 0,
    RUNTIME_LED_COLOR_PIECE,
    RUNTIME_LED_COLOR_LIFTED,
    RUNTIME_LED_COLOR_LEGAL,
    RUNTIME_LED_COLOR_BEST,
    RUNTIME_LED_COLOR_INVALID,
    RUNTIME_LED_COLOR_CHECK,
    RUNTIME_LED_COLOR_DRAW,
    RUNTIME_LED_COLOR_COUNT
} runtime_led_color_id_t;

typedef struct
{
    uint8_t invalid_position_infractions_enabled;
    uint8_t led_empty_enabled;
    uint8_t stockfish_enabled;
    uint8_t stockfish_depth;
    uint16_t clock_initial_seconds;
    uint16_t clock_bonus_seconds;
    uint8_t led_brightness_percent;

    project_rgb_t led_empty_rgb;
    project_rgb_t led_piece_rgb;
    project_rgb_t led_lifted_rgb;
    project_rgb_t led_legal_rgb;
    project_rgb_t led_best_rgb;
    project_rgb_t led_invalid_rgb;
    project_rgb_t led_check_rgb;
    project_rgb_t led_draw_rgb;
} runtime_config_t;

const runtime_config_t * runtimeConfigGet(void);
esp_err_t runtimeConfigLoadFromNvs(void);
esp_err_t runtimeConfigSave(void);
esp_err_t runtimeConfigReset(void);
esp_err_t runtimeConfigSetInvalidPositionInfractions(uint8_t enabled);
esp_err_t runtimeConfigSetLedEmptyEnabled(uint8_t enabled);
esp_err_t runtimeConfigSetStockfishEnabled(uint8_t enabled);
esp_err_t runtimeConfigSetStockfishDepth(uint8_t depth);
esp_err_t runtimeConfigSetClockInitialSeconds(uint16_t seconds);
esp_err_t runtimeConfigSetClockBonusSeconds(uint16_t seconds);
esp_err_t runtimeConfigSetLedBrightness(uint8_t percent);
esp_err_t runtimeConfigSetLedColor(runtime_led_color_id_t colorId, project_rgb_t color);
project_rgb_t runtimeConfigColor(runtime_led_color_id_t colorId);

#endif
