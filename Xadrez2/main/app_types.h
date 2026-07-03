#ifndef APP_TYPES_H
#define APP_TYPES_H

#include <stdint.h>

#define APP_SQUARE_TEXT_LEN       (3U)
#define APP_MOVE_TEXT_LEN         (6U)
#define APP_MAX_LEGAL_MOVES       (8U)

typedef enum
{
    SENSOR_STATE_PRESENT = 0,
    SENSOR_STATE_LIFTED = 1
} sensor_piece_state_t;

typedef struct
{
    uint32_t sequence;
    sensor_piece_state_t state;
    char square[APP_SQUARE_TEXT_LEN];
} sensor_event_t;

typedef struct
{
    uint32_t sequence;
    uint8_t clear;
    uint8_t bestValid;
    uint32_t legalCount;
    char legal[APP_MAX_LEGAL_MOVES][APP_SQUARE_TEXT_LEN];
    char bestFrom[APP_SQUARE_TEXT_LEN];
    char bestTo[APP_SQUARE_TEXT_LEN];
} led_command_t;

#endif
