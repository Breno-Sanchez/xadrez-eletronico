#ifndef PROJECT_CONFIG_H
#define PROJECT_CONFIG_H

#include <stdint.h>

#define PROJECT_CONFIG_URL_LEN (160U)

typedef struct
{
    uint8_t r;
    uint8_t g;
    uint8_t b;
} project_rgb_t;

typedef struct
{
    uint8_t stockfish_enabled;
    char stockfish_url[PROJECT_CONFIG_URL_LEN];
    uint8_t stockfish_depth;
    uint32_t stockfish_timeout_ms;
    uint32_t stockfish_response_bytes;

    uint32_t led_gpio;
    uint32_t led_physical_count;
    uint8_t led_brightness_percent;

    project_rgb_t led_empty_rgb;
    project_rgb_t led_piece_rgb;
    project_rgb_t led_lifted_rgb;
    project_rgb_t led_legal_rgb;
    project_rgb_t led_best_rgb;
    project_rgb_t led_invalid_rgb;
    project_rgb_t led_check_rgb;
    project_rgb_t led_draw_rgb;
} project_config_t;

const project_config_t * projectConfigGet(void);

#endif
