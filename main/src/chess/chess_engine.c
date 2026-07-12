#include "chess_engine.h"

#include <stdbool.h>
#include <stdint.h>

bool chess_engine_best_for_origin(const chess_game_t * game,
                                  uint8_t row,
                                  uint8_t col,
                                  chess_move_t * best_move)
{
    (void)game;
    (void)row;
    (void)col;
    (void)best_move;

    return false;
}

bool chess_engine_best_global(const chess_game_t * game, chess_move_t * best_move)
{
    (void)game;
    (void)best_move;

    return false;
}
