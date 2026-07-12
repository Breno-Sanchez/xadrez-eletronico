#ifndef STOCKFISH_CLIENT_H
#define STOCKFISH_CLIENT_H

#include <stdbool.h>
#include <stdint.h>

#include "app_types.h"

#define STOCKFISH_CLIENT_JSON_TEXT_LEN (1536U)

typedef struct
{
    char bestMove[APP_MOVE_TEXT_LEN];
    char json[STOCKFISH_CLIENT_JSON_TEXT_LEN];
    uint8_t valid;
} stockfish_client_result_t;

bool stockfishClientAnalyzeFen(const char * fen, stockfish_client_result_t * result);
bool stockfishClientBestMove(const char * fen, char bestMove[APP_MOVE_TEXT_LEN]);

#endif
