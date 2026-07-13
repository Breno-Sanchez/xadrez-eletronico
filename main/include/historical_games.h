#ifndef HISTORICAL_GAMES_H
#define HISTORICAL_GAMES_H

#include <stdint.h>

#include "chess_logic.h"
#include "esp_err.h"

#define HISTORICAL_GAME_MAX_COUNT      (10U)
#define HISTORICAL_GAME_TITLE_LEN      (96U)
#define HISTORICAL_GAME_META_LEN       (64U)
#define HISTORICAL_GAME_RESULT_LEN     (8U)
#define HISTORICAL_GAME_SAN_LEN        (16U)
#define HISTORICAL_GAME_UCI_LEN        (8U)
#define HISTORICAL_GAME_MAX_PLIES      (160U)

typedef struct
{
    chess_move_t move;
    char san[HISTORICAL_GAME_SAN_LEN];
    char uci[HISTORICAL_GAME_UCI_LEN];
} historical_ply_t;

typedef struct
{
    char event[HISTORICAL_GAME_META_LEN];
    char site[HISTORICAL_GAME_META_LEN];
    char date[HISTORICAL_GAME_META_LEN];
    char white[HISTORICAL_GAME_META_LEN];
    char black[HISTORICAL_GAME_META_LEN];
    char result[HISTORICAL_GAME_RESULT_LEN];
    char title[HISTORICAL_GAME_TITLE_LEN];
    uint16_t ply_count;
    historical_ply_t plies[HISTORICAL_GAME_MAX_PLIES];
} historical_game_t;

esp_err_t historicalGamesInit(void);
uint8_t historicalGamesCount(void);
const historical_game_t * historicalGameGet(uint8_t index);

#endif
