#include "historical_games.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern const uint8_t _binary_historical_plays_json_start[] asm("_binary_historical_plays_json_start");
extern const uint8_t _binary_historical_plays_json_end[] asm("_binary_historical_plays_json_end");

static historical_game_t games[HISTORICAL_GAME_MAX_COUNT];
static uint8_t gameCount = 0U;
static uint8_t loaded = 0U;

static bool isSpaceChar(char ch)
{
    return ((ch == ' ') || (ch == '\n') || (ch == '\r') || (ch == '\t'));
}

static bool isResultToken(const char * text)
{
    bool result = false;

    if (text != NULL)
    {
        if ((strcmp(text, "1-0") == 0) ||
            (strcmp(text, "0-1") == 0) ||
            (strcmp(text, "1/2-1/2") == 0) ||
            (strcmp(text, "*") == 0))
        {
            result = true;
        }
    }

    return result;
}

static chess_piece_type_t pieceTypeFromSan(char ch)
{
    chess_piece_type_t type = CHESS_PAWN;

    switch (ch)
    {
        case 'K': type = CHESS_KING; break;
        case 'Q': type = CHESS_QUEEN; break;
        case 'R': type = CHESS_ROOK; break;
        case 'B': type = CHESS_BISHOP; break;
        case 'N': type = CHESS_KNIGHT; break;
        default:  type = CHESS_PAWN; break;
    }

    return type;
}

static chess_piece_type_t promotionFromChar(char ch)
{
    chess_piece_type_t type = CHESS_EMPTY;

    switch (ch)
    {
        case 'Q': type = CHESS_QUEEN; break;
        case 'R': type = CHESS_ROOK; break;
        case 'B': type = CHESS_BISHOP; break;
        case 'N': type = CHESS_KNIGHT; break;
        default:  type = CHESS_EMPTY; break;
    }

    return type;
}

static void copyBounded(char * dst, size_t dstLen, const char * src)
{
    size_t pos = 0U;

    if ((dst == NULL) || (src == NULL) || (dstLen == 0U))
    {
        return;
    }

    while ((src[pos] != '\0') && (pos < (dstLen - 1U)))
    {
        dst[pos] = src[pos];
        pos++;
    }

    dst[pos] = '\0';
}

static const char * findJsonKey(const char * begin, const char * end, const char * key)
{
    const char * pos = begin;
    char pattern[40];

    if ((begin == NULL) || (end == NULL) || (key == NULL))
    {
        return NULL;
    }

    (void)snprintf(pattern, sizeof(pattern), "\"%s\"", key);

    while (pos < end)
    {
        const char * found = strstr(pos, pattern);

        if ((found == NULL) || (found >= end))
        {
            return NULL;
        }

        found += strlen(pattern);

        while ((found < end) && ((*found == ' ') || (*found == '\t') || (*found == '\r') || (*found == '\n')))
        {
            found++;
        }

        if ((found < end) && (*found == ':'))
        {
            found++;
            while ((found < end) && isSpaceChar(*found))
            {
                found++;
            }

            return found;
        }

        pos = found;
    }

    return NULL;
}

static bool readJsonString(const char * begin, const char * end, const char * key, char * dst, size_t dstLen)
{
    const char * value;
    size_t out = 0U;

    if ((dst == NULL) || (dstLen == 0U))
    {
        return false;
    }

    dst[0] = '\0';
    value = findJsonKey(begin, end, key);

    if ((value == NULL) || (value >= end) || (*value != '"'))
    {
        return false;
    }

    value++;

    while ((value < end) && (*value != '\0') && (*value != '"') && (out < (dstLen - 1U)))
    {
        if ((*value == '\\') && ((value + 1) < end))
        {
            value++;
        }

        dst[out] = *value;
        out++;
        value++;
    }

    dst[out] = '\0';
    return true;
}

static void moveToUci(const chess_move_t * move, char out[HISTORICAL_GAME_UCI_LEN])
{
    char from[3];
    char to[3];

    if ((move == NULL) || (out == NULL))
    {
        return;
    }

    chess_index_to_square(move->from_row, move->from_col, from);
    chess_index_to_square(move->to_row, move->to_col, to);

    (void)snprintf(out, HISTORICAL_GAME_UCI_LEN, "%s%s", from, to);

    if (move->promotion != CHESS_EMPTY)
    {
        char suffix = 'q';

        if (move->promotion == CHESS_ROOK) suffix = 'r';
        else if (move->promotion == CHESS_BISHOP) suffix = 'b';
        else if (move->promotion == CHESS_KNIGHT) suffix = 'n';

        out[4] = suffix;
        out[5] = '\0';
    }
}

static void normalizeSanToken(const char * token, char out[HISTORICAL_GAME_SAN_LEN])
{
    size_t src = 0U;
    size_t dst = 0U;
    size_t len;

    if ((token == NULL) || (out == NULL))
    {
        return;
    }

    while ((token[src] >= '0') && (token[src] <= '9'))
    {
        src++;
    }

    while (token[src] == '.')
    {
        src++;
    }

    while ((token[src] != '\0') && (dst < (HISTORICAL_GAME_SAN_LEN - 1U)))
    {
        out[dst] = token[src];
        dst++;
        src++;
    }

    out[dst] = '\0';
    len = strlen(out);

    while (len > 0U)
    {
        char ch = out[len - 1U];

        if ((ch == '+') || (ch == '#') || (ch == '!') || (ch == '?') || (ch == ',') || (ch == ';'))
        {
            out[len - 1U] = '\0';
            len--;
        }
        else
        {
            break;
        }
    }
}

static bool sanToMove(const chess_game_t * game, const char * sanInput, chess_move_t * outMove)
{
    char san[HISTORICAL_GAME_SAN_LEN];
    char core[HISTORICAL_GAME_SAN_LEN];
    char dstSquare[3] = "";
    char disamb[4] = "";
    uint8_t dstRow = 0U;
    uint8_t dstCol = 0U;
    uint8_t moveCount;
    chess_move_t moves[CHESS_MAX_MOVES];
    chess_piece_type_t targetType = CHESS_PAWN;
    chess_piece_type_t promotion = CHESS_EMPTY;
    bool castleKingSide = false;
    bool castleQueenSide = false;
    bool found = false;
    uint8_t start = 0U;
    size_t sanLen;
    size_t coreLen = 0U;
    size_t destPos = 0U;

    if ((game == NULL) || (sanInput == NULL) || (outMove == NULL))
    {
        return false;
    }

    normalizeSanToken(sanInput, san);
    sanLen = strlen(san);

    if ((sanLen == 0U) || (isResultToken(san) == true))
    {
        return false;
    }

    if ((strcmp(san, "O-O") == 0) || (strcmp(san, "0-0") == 0))
    {
        castleKingSide = true;
    }
    else if ((strcmp(san, "O-O-O") == 0) || (strcmp(san, "0-0-0") == 0))
    {
        castleQueenSide = true;
    }

    moveCount = (uint8_t)chess_generate_all_legal_moves(game, moves, CHESS_MAX_MOVES);

    if ((castleKingSide == true) || (castleQueenSide == true))
    {
        for (uint8_t i = 0U; i < moveCount; i++)
        {
            chess_piece_t piece = chess_get_piece(game, moves[i].from_row, moves[i].from_col);
            int32_t dc = (int32_t)moves[i].to_col - (int32_t)moves[i].from_col;

            if ((piece.type == CHESS_KING) &&
                (((castleKingSide == true) && (dc == 2)) ||
                 ((castleQueenSide == true) && (dc == -2))))
            {
                *outMove = moves[i];
                return true;
            }
        }

        return false;
    }

    for (size_t i = 0U; (i < sanLen) && (coreLen < (sizeof(core) - 1U)); i++)
    {
        char ch = san[i];

        if ((ch != 'x') && (ch != '+') && (ch != '#') && (ch != '!') && (ch != '?'))
        {
            core[coreLen] = ch;
            coreLen++;
        }
    }

    core[coreLen] = '\0';

    for (size_t i = 0U; i < coreLen; i++)
    {
        if (core[i] == '=')
        {
            if ((i + 1U) < coreLen)
            {
                promotion = promotionFromChar(core[i + 1U]);
            }

            core[i] = '\0';
            coreLen = strlen(core);
            break;
        }
    }

    if ((core[0] == 'K') || (core[0] == 'Q') || (core[0] == 'R') || (core[0] == 'B') || (core[0] == 'N'))
    {
        targetType = pieceTypeFromSan(core[0]);
        start = 1U;
    }

    if (coreLen < 2U)
    {
        return false;
    }

    for (size_t i = coreLen; i > 1U; i--)
    {
        if ((core[i - 2U] >= 'a') && (core[i - 2U] <= 'h') &&
            (core[i - 1U] >= '1') && (core[i - 1U] <= '8'))
        {
            dstSquare[0] = core[i - 2U];
            dstSquare[1] = core[i - 1U];
            dstSquare[2] = '\0';
            destPos = i - 2U;
            break;
        }
    }

    if ((dstSquare[0] == '\0') || (chess_square_to_index(dstSquare, &dstRow, &dstCol) == false))
    {
        return false;
    }

    if ((destPos > start) && ((destPos - start) < sizeof(disamb)))
    {
        for (size_t i = start; i < destPos; i++)
        {
            disamb[i - start] = core[i];
        }

        disamb[destPos - start] = '\0';
    }

    for (uint8_t i = 0U; i < moveCount; i++)
    {
        chess_piece_t piece = chess_get_piece(game, moves[i].from_row, moves[i].from_col);
        bool match = true;

        if ((piece.type != targetType) ||
            (moves[i].to_row != dstRow) ||
            (moves[i].to_col != dstCol))
        {
            match = false;
        }

        if ((match == true) && (promotion != CHESS_EMPTY) && (moves[i].promotion != promotion))
        {
            match = false;
        }

        if ((match == true) && (disamb[0] != '\0'))
        {
            for (size_t d = 0U; disamb[d] != '\0'; d++)
            {
                if ((disamb[d] >= 'a') && (disamb[d] <= 'h'))
                {
                    if (moves[i].from_col != (uint8_t)(disamb[d] - 'a'))
                    {
                        match = false;
                    }
                }
                else if ((disamb[d] >= '1') && (disamb[d] <= '8'))
                {
                    uint8_t row = (uint8_t)('8' - disamb[d]);

                    if (moves[i].from_row != row)
                    {
                        match = false;
                    }
                }
            }
        }

        if (match == true)
        {
            *outMove = moves[i];
            found = true;
            break;
        }
    }

    return found;
}

static bool appendSanMove(historical_game_t * historical, chess_game_t * sim, const char * token)
{
    char san[HISTORICAL_GAME_SAN_LEN];
    chess_move_t move;

    if ((historical == NULL) || (sim == NULL) || (token == NULL) ||
        (historical->ply_count >= HISTORICAL_GAME_MAX_PLIES))
    {
        return false;
    }

    normalizeSanToken(token, san);

    if ((san[0] == '\0') || (isResultToken(san) == true))
    {
        return true;
    }

    if (sanToMove(sim, san, &move) == false)
    {
        return false;
    }

    historical->plies[historical->ply_count].move = move;
    copyBounded(historical->plies[historical->ply_count].san, sizeof(historical->plies[historical->ply_count].san), san);
    moveToUci(&move, historical->plies[historical->ply_count].uci);

    if (chess_apply_move(sim, &move) == false)
    {
        return false;
    }

    historical->ply_count++;
    return true;
}

static void parseMoves(historical_game_t * historical, const char * movesText)
{
    chess_game_t sim;
    char token[HISTORICAL_GAME_SAN_LEN];
    size_t pos = 0U;
    size_t tokenPos = 0U;

    if ((historical == NULL) || (movesText == NULL))
    {
        return;
    }

    chess_reset(&sim);

    while (movesText[pos] != '\0')
    {
        char ch = movesText[pos];

        if (isSpaceChar(ch) == true)
        {
            if (tokenPos > 0U)
            {
                token[tokenPos] = '\0';
                (void)appendSanMove(historical, &sim, token);
                tokenPos = 0U;
            }
        }
        else if (tokenPos < (sizeof(token) - 1U))
        {
            token[tokenPos] = ch;
            tokenPos++;
        }

        pos++;
    }

    if (tokenPos > 0U)
    {
        token[tokenPos] = '\0';
        (void)appendSanMove(historical, &sim, token);
    }
}

static void buildTitle(historical_game_t * historical)
{
    char title[HISTORICAL_GAME_TITLE_LEN];

    if (historical == NULL)
    {
        return;
    }

    if (historical->title[0] != '\0')
    {
        return;
    }

    title[0] = '\0';

    if ((historical->white[0] != '\0') || (historical->black[0] != '\0') || (historical->event[0] != '\0'))
    {
        (void)snprintf(
            title,
            sizeof(title),
            "%s vs %s - %s",
            historical->white,
            historical->black,
            historical->event
        );
    }
    else
    {
        (void)snprintf(title, sizeof(title), "Historical chess game");
    }

    copyBounded(historical->title, sizeof(historical->title), title);
}

esp_err_t historicalGamesInit(void)
{
    const char * json = (const char *)_binary_historical_plays_json_start;
    const char * end = (const char *)_binary_historical_plays_json_end;
    const char * pos;
    const char * objectStart;

    if (loaded != 0U)
    {
        return ESP_OK;
    }

    gameCount = 0U;
    (void)memset(games, 0, sizeof(games));

    pos = json;

    while ((pos < end) && (gameCount < HISTORICAL_GAME_MAX_COUNT))
    {
        objectStart = strchr(pos, '{');

        if ((objectStart == NULL) || (objectStart >= end))
        {
            break;
        }

        const char * objectEnd = strchr(objectStart, '}');

        if ((objectEnd == NULL) || (objectEnd >= end))
        {
            break;
        }

        (void)readJsonString(objectStart, objectEnd, "event", games[gameCount].event, sizeof(games[gameCount].event));
        (void)readJsonString(objectStart, objectEnd, "site", games[gameCount].site, sizeof(games[gameCount].site));
        (void)readJsonString(objectStart, objectEnd, "date", games[gameCount].date, sizeof(games[gameCount].date));
        (void)readJsonString(objectStart, objectEnd, "white", games[gameCount].white, sizeof(games[gameCount].white));
        (void)readJsonString(objectStart, objectEnd, "black", games[gameCount].black, sizeof(games[gameCount].black));
        (void)readJsonString(objectStart, objectEnd, "result", games[gameCount].result, sizeof(games[gameCount].result));
        (void)readJsonString(objectStart, objectEnd, "title", games[gameCount].title, sizeof(games[gameCount].title));

        char movesText[2048];
        movesText[0] = '\0';

        if ((readJsonString(objectStart, objectEnd, "moves", movesText, sizeof(movesText)) == true) ||
            (readJsonString(objectStart, objectEnd, "pgn", movesText, sizeof(movesText)) == true) ||
            (readJsonString(objectStart, objectEnd, "PGN", movesText, sizeof(movesText)) == true) ||
            (readJsonString(objectStart, objectEnd, "notation", movesText, sizeof(movesText)) == true))
        {
            parseMoves(&games[gameCount], movesText);
        }

        buildTitle(&games[gameCount]);

        if (games[gameCount].ply_count > 0U)
        {
            gameCount++;
        }

        pos = objectEnd + 1;
    }

    loaded = 1U;
    return ESP_OK;
}

uint8_t historicalGamesCount(void)
{
    (void)historicalGamesInit();
    return gameCount;
}

const historical_game_t * historicalGameGet(uint8_t index)
{
    (void)historicalGamesInit();

    if (index >= gameCount)
    {
        return NULL;
    }

    return &games[index];
}
