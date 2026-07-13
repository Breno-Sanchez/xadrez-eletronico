#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "app_types.h"
#include "chess_logic.h"
#include "credentials.h"
#include "esp_eap_client.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "led.h"
#include "nvs_flash.h"
#include "server.h"
#include "stockfish_client.h"
#include "runtime_config.h"

#define WIFI_SSID_TXT              "PROVISION_WITH_RUN_CHESS"
#define EAP_IDENTITY_TXT           "PROVISION_WITH_RUN_CHESS"
#define EAP_USERNAME_TXT           "PROVISION_WITH_RUN_CHESS"
#define EAP_PASSWORD_TXT           "PROVISION_WITH_RUN_CHESS"
#define SOFTAP_SSID_TXT            "XADREZ_ESP"
#define SOFTAP_PASS_TXT            "xadrez12345"
#define SOFTAP_CHANNEL             (1U)
#define SOFTAP_MAX_CONNECTIONS     (2U)
#define EAP_MAX_LEN                (127U)
#define STATE_TEXT_LEN             (32U)
#define MODE_TEXT_LEN              (32U)
#define LEGAL_TEXT_LEN             (256U)
#define HTTP_CHUNK_LEN             (768U)
#define GAME_COMMAND_QUEUE_LEN     (8U)
#define START_RAINBOW_MS           (700U)
#define STOCKFISH_REQUEST_QUEUE_LEN  (1U)
#define STOCKFISH_RESPONSE_QUEUE_LEN (1U)
#define STOCKFISH_TASK_STACK_BYTES   (16384U)
#define STOCKFISH_TASK_PRIORITY      (2U)
#define SERVER_STACK_WORD_COUNT(bytes_) (((bytes_) + sizeof(StackType_t) - 1U) / sizeof(StackType_t))
#define CAPTURED_PIECE_SLOT_COUNT  (7U)
#define CLOCK_CONFIG_MAX_SECONDS    (21600U)
#define CLOCK_BONUS_MAX_SECONDS     (600U)

typedef enum
{
    GAME_MODE_SETUP = 0,
    GAME_MODE_RUNNING,
    GAME_MODE_LIFTED,
    GAME_MODE_PROMOTION_PENDING,
    GAME_MODE_INVALID_LOCK,
    GAME_MODE_SYNC_WAIT,
    GAME_MODE_CHECKMATE_LOCK,
    GAME_MODE_DRAW_LOCK
} game_mode_t;

typedef enum
{
    GAME_COMMAND_START = 0,
    GAME_COMMAND_PROMOTE,
    GAME_COMMAND_DRAW
} game_command_type_t;

typedef struct
{
    game_command_type_t type;
    chess_piece_type_t promotion;
} game_command_t;

typedef struct
{
    uint32_t requestId;
    char fen[CHESS_FEN_LEN];
} stockfish_request_t;

typedef struct
{
    uint32_t requestId;
    char fen[CHESS_FEN_LEN];
    stockfish_client_result_t result;
} stockfish_response_t;

static const char * const TAG = "SERVER";

static QueueHandle_t sensorQueueRef = NULL;
static QueueHandle_t ledQueueRef = NULL;
static QueueHandle_t gameCommandQueue = NULL;
static QueueHandle_t stockfishRequestQueue = NULL;
static QueueHandle_t stockfishResponseQueue = NULL;
static SemaphoreHandle_t stateMutex = NULL;
static httpd_handle_t httpServer = NULL;
static uint8_t wifiConnected = 0U;
static wifi_enterprise_credentials_t enterpriseCredentials;
static uint8_t enterpriseCredentialsReady = 0U;

static StaticQueue_t gameCommandQueueControl;
static uint8_t gameCommandQueueStorage[GAME_COMMAND_QUEUE_LEN * sizeof(game_command_t)];

static StaticQueue_t stockfishRequestQueueControl;
static StaticQueue_t stockfishResponseQueueControl;
static uint8_t stockfishRequestQueueStorage[STOCKFISH_REQUEST_QUEUE_LEN * sizeof(stockfish_request_t)];
static uint8_t stockfishResponseQueueStorage[STOCKFISH_RESPONSE_QUEUE_LEN * sizeof(stockfish_response_t)];
static StackType_t stockfishTaskStack[SERVER_STACK_WORD_COUNT(STOCKFISH_TASK_STACK_BYTES)];
static StaticTask_t stockfishTaskControlBlock;
static TaskHandle_t stockfishTaskHandle = NULL;

static chess_game_t game;
static game_mode_t gameMode = GAME_MODE_SETUP;
static uint8_t physicalPresence[APP_BOARD_RANK_COUNT] = {0U};
static uint8_t legalMap[APP_BOARD_RANK_COUNT] = {0U};
static uint8_t bestMap[APP_BOARD_RANK_COUNT] = {0U};
static uint8_t invalidMap[APP_BOARD_RANK_COUNT] = {0U};
static uint8_t checkMap[APP_BOARD_RANK_COUNT] = {0U};
static uint8_t selectedValid = 0U;
static char selectedSquare[APP_SQUARE_TEXT_LEN] = "";
static chess_move_t pendingPromotionMove;
static uint8_t pendingPromotionValid = 0U;
static char stateText[STATE_TEXT_LEN] = "SETUP";
static char stateLegal[LEGAL_TEXT_LEN] = "-";
static char stateBest[APP_MOVE_TEXT_LEN] = "-----";
static uint8_t winnerWhite = 0U;
static uint8_t whiteInfractions = 0U;
static uint8_t blackInfractions = 0U;
static uint8_t drawOfferActive = 0U;
static uint8_t drawOfferByWhite = 0U;
static uint8_t capturedByWhiteCounts[CAPTURED_PIECE_SLOT_COUNT] = {0U};
static uint8_t capturedByBlackCounts[CAPTURED_PIECE_SLOT_COUNT] = {0U};
static uint32_t ledSequence = 1U;
static uint8_t orientationKnown = 0U;
static uint8_t orientationFlipRanks = 0U;
static uint32_t stockfishRequestSequence = 0U;
static uint8_t stockfishBestValid = 0U;
static char stockfishBestFen[CHESS_FEN_LEN] = "";
static char stockfishBestMove[APP_MOVE_TEXT_LEN] = "";
static char stateStockfishJson[STOCKFISH_CLIENT_JSON_TEXT_LEN] = "";
static uint32_t whiteClockMs = 0U;
static uint32_t blackClockMs = 0U;
static uint32_t clockLastTickMs = 0U;
static uint8_t clockActive = 0U;

static size_t boundedStringLength(const char * text, size_t maxLen);
static void copyStringToU8(uint8_t * dst, size_t dstLen, const char * src);
static void setBit(uint8_t map[APP_BOARD_RANK_COUNT], const char square[APP_SQUARE_TEXT_LEN]);
static void clearMaps(void);
static void updatePhysical(const char square[APP_SQUARE_TEXT_LEN], uint8_t present);
static void rebuildOwnerMaps(led_command_t * command);
static void sendLedFrame(uint8_t rainbowActive);
static void setMode(game_mode_t mode, const char * text);
static bool physicalMatchesStart(void);
static bool physicalMatchesBoard(void);
static void updateSetupFeedback(void);
static bool physicalSquareToVirtualIndex(const char square[APP_SQUARE_TEXT_LEN], uint8_t * row, uint8_t * col);
static void virtualIndexToPhysicalSquare(uint8_t row, uint8_t col, char square[APP_SQUARE_TEXT_LEN]);
static void setVirtualBit(uint8_t map[APP_BOARD_RANK_COUNT], uint8_t row, uint8_t col);
static bool selectOrientationForLift(const char square[APP_SQUARE_TEXT_LEN], uint8_t * row, uint8_t * col, chess_piece_t * piece);
static void startGame(void);
static void declareDraw(void);
static void clockResetForNewGame(void);
static void clockUpdate(void);
static void clockApplyIncrement(chess_color_t side);
static void declareTimeLoss(chess_color_t losingSide);
static void handleSensorEvent(const sensor_event_t * event);
static void handleGameCommand(const game_command_t * command);
static void processPendingCommands(void);
static void stockfishTask(void * parameters);
static void processStockfishResponses(void);
static void requestStockfishBestAsync(void);
static void refreshStockfishAdvisorAfterConfig(uint8_t wasEnabled);
static void refreshStockfishBestMap(void);
static void clearStockfishBest(void);
static bool isStockfishMoveText(const char * text);
static void computeLiftHints(uint8_t row, uint8_t col);
static bool applySelectedMove(uint8_t toRow, uint8_t toCol, chess_piece_type_t promotion);
static chess_piece_type_t selectorPromotionPiece(const char square[APP_SQUARE_TEXT_LEN]);
static void updateCheckState(void);
static void updatePostMoveTerminalState(void);
static void updateLegalTextFromMap(void);
static void clearCapturedMaterial(void);
static void recordCapturedPiece(chess_color_t capturingSide, chess_piece_t capturedPiece);
static const char * modeToText(game_mode_t mode);
static const char * orientationText(void);
static const char * sensorStateText(sensor_piece_state_t state);
static const char * pieceTypeText(chess_piece_type_t type);
static const char * pieceColorText(chess_color_t color);
static void indexToText(uint8_t row, uint8_t col, char out[APP_SQUARE_TEXT_LEN]);
static void logBoardEvent(const sensor_event_t * event);

static esp_err_t initNvs(void);
static esp_err_t initEnterpriseAuth(const wifi_enterprise_credentials_t * credentials);
static void eventHandler(void * arg, esp_event_base_t base, int32_t id, void * data);
static esp_err_t startHttpServer(void);
static esp_err_t rootHandler(httpd_req_t * req);
static esp_err_t apiStateHandler(httpd_req_t * req);
static esp_err_t apiStockfishHandler(httpd_req_t * req);
static esp_err_t apiConfigHandler(httpd_req_t * req);
static esp_err_t apiStartHandler(httpd_req_t * req);
static esp_err_t apiPromoteHandler(httpd_req_t * req);
static esp_err_t apiDrawHandler(httpd_req_t * req);

static const httpd_uri_t rootUri = {
 .uri = "/", .method = HTTP_GET, .handler = rootHandler, .user_ctx = NULL };
static const httpd_uri_t stateUri = { .uri = "/api/state", .method = HTTP_GET, .handler = apiStateHandler, .user_ctx = NULL };
static const httpd_uri_t stockfishUri = { .uri = "/api/stockfish", .method = HTTP_GET, .handler = apiStockfishHandler, .user_ctx = NULL };
static const httpd_uri_t configGetUri = { .uri = "/api/config", .method = HTTP_GET, .handler = apiConfigHandler, .user_ctx = NULL };
static const httpd_uri_t configPostUri = { .uri = "/api/config", .method = HTTP_POST, .handler = apiConfigHandler, .user_ctx = NULL };
static const httpd_uri_t startUri = { .uri = "/api/start", .method = HTTP_POST, .handler = apiStartHandler, .user_ctx = NULL };
static const httpd_uri_t promoteUri = { .uri = "/api/promote", .method = HTTP_POST, .handler = apiPromoteHandler, .user_ctx = NULL };
static const httpd_uri_t drawUri = { .uri = "/api/draw", .method = HTTP_POST, .handler = apiDrawHandler, .user_ctx = NULL };

static size_t boundedStringLength(const char * text, size_t maxLen)
{
    size_t len = 0U;
    if (text != NULL)
    {
        while ((len < maxLen) && (text[len] != '\0'))
        {
            len++;
        }
    }
    return len;
}

static void copyStringToU8(uint8_t * dst, size_t dstLen, const char * src)
{
    size_t index = 0U;
    if ((dst != NULL) && (src != NULL) && (dstLen > 0U))
    {
        while ((src[index] != '\0') && (index < (dstLen - 1U)))
        {
            dst[index] = (uint8_t)src[index];
            index++;
        }
        dst[index] = (uint8_t)'\0';
    }
}

static void setMode(game_mode_t mode, const char * text)
{
    game_mode_t oldMode = gameMode;
    char oldText[STATE_TEXT_LEN];

    (void)snprintf(oldText, sizeof(oldText), "%s", stateText);

    gameMode = mode;

    if ((mode == GAME_MODE_SETUP) ||
        (mode == GAME_MODE_CHECKMATE_LOCK) ||
        (mode == GAME_MODE_DRAW_LOCK))
    {
        /* CLOCK_STOP_ON_TERMINAL_MODE */
        clockActive = 0U;
    }
    if (text != NULL)
    {
        (void)snprintf(stateText, sizeof(stateText), "%s", text);
    }

    if ((oldMode != gameMode) || (strcmp(oldText, stateText) != 0))
    {
        ESP_LOGI(
            TAG,
            "STATE mode=%s->%s state=%s turn=%s orientation=%s selected=%s fen=%s",
            modeToText(oldMode),
            modeToText(gameMode),
            stateText,
            pieceColorText(game.side_to_move),
            orientationText(),
            (selectedValid != 0U) ? selectedSquare : "-",
            game.fen
        );
    }
}

static const char * modeToText(game_mode_t mode)
{
    switch (mode)
    {
        case GAME_MODE_SETUP: return "SETUP";
        case GAME_MODE_RUNNING: return "RUNNING";
        case GAME_MODE_LIFTED: return "LIFTED";
        case GAME_MODE_PROMOTION_PENDING: return "PROMOTION";
        case GAME_MODE_INVALID_LOCK: return "INVALID_LOCK";
        case GAME_MODE_SYNC_WAIT: return "SYNC_WAIT";
        case GAME_MODE_CHECKMATE_LOCK: return "CHECKMATE";
        case GAME_MODE_DRAW_LOCK: return "DRAW";
        default: return "UNKNOWN";
    }
}

static const char * orientationText(void)
{
    const char * text = "unassigned";

    if (orientationKnown != 0U)
    {
        text = (orientationFlipRanks != 0U) ? "flipped" : "normal";
    }

    return text;
}

static const char * sensorStateText(sensor_piece_state_t state)
{
    const char * text = "UNKNOWN";

    if (state == SENSOR_STATE_PRESENT)
    {
        text = "PRESENT";
    }
    else if (state == SENSOR_STATE_LIFTED)
    {
        text = "REMOVED";
    }

    return text;
}

static const char * pieceTypeText(chess_piece_type_t type)
{
    const char * text = "UNKNOWN";

    switch (type)
    {
        case CHESS_EMPTY:
            text = "EMPTY";
            break;

        case CHESS_PAWN:
            text = "PAWN";
            break;

        case CHESS_KNIGHT:
            text = "KNIGHT";
            break;

        case CHESS_BISHOP:
            text = "BISHOP";
            break;

        case CHESS_ROOK:
            text = "ROOK";
            break;

        case CHESS_QUEEN:
            text = "QUEEN";
            break;

        case CHESS_KING:
            text = "KING";
            break;

        default:
            text = "UNKNOWN";
            break;
    }

    return text;
}

static const char * pieceColorText(chess_color_t color)
{
    const char * text = "none";

    if (color == CHESS_WHITE)
    {
        text = "white";
    }
    else if (color == CHESS_BLACK)
    {
        text = "black";
    }

    return text;
}

static void indexToText(uint8_t row, uint8_t col, char out[APP_SQUARE_TEXT_LEN])
{
    if (out == NULL)
    {
        return;
    }

    if ((row < APP_BOARD_RANK_COUNT) && (col < APP_BOARD_FILE_COUNT))
    {
        chess_index_to_square(row, col, out);
    }
    else
    {
        out[0] = '?';
        out[1] = '?';
        out[2] = '\0';
    }
}

static void logBoardEvent(const sensor_event_t * event)
{
    if (event == NULL)
    {
        return;
    }

    ESP_LOGI(
        TAG,
        "EVENT seq=%lu physical=%s state=%s mode=%s turn=%s orientation=%s selected=%s fen=%s",
        (unsigned long)event->sequence,
        event->square,
        sensorStateText(event->state),
        modeToText(gameMode),
        pieceColorText(game.side_to_move),
        orientationText(),
        (selectedValid != 0U) ? selectedSquare : "-",
        game.fen
    );
}


static void setBit(uint8_t map[APP_BOARD_RANK_COUNT], const char square[APP_SQUARE_TEXT_LEN])
{
    if ((map != NULL) && (square != NULL) &&
        (square[0] >= 'a') && (square[0] <= 'h') &&
        (square[1] >= '1') && (square[1] <= '8'))
    {
        uint32_t file = (uint32_t)(square[0] - 'a');
        uint32_t rank = (uint32_t)(square[1] - '1');
        map[rank] = (uint8_t)(map[rank] | ((uint8_t)(1U << file)));
    }
}

static void clearMaps(void)
{
    (void)memset(legalMap, 0, sizeof(legalMap));
    (void)memset(bestMap, 0, sizeof(bestMap));
    (void)memset(invalidMap, 0, sizeof(invalidMap));
    (void)memset(checkMap, 0, sizeof(checkMap));
    stateLegal[0] = '-';
    stateLegal[1] = '\0';
    (void)snprintf(stateBest, sizeof(stateBest), "-----");
}

static void updatePhysical(const char square[APP_SQUARE_TEXT_LEN], uint8_t present)
{
    if ((square != NULL) && (square[0] >= 'a') && (square[0] <= 'h') && (square[1] >= '1') && (square[1] <= '8'))
    {
        uint32_t file = (uint32_t)(square[0] - 'a');
        uint32_t rank = (uint32_t)(square[1] - '1');
        uint8_t mask = (uint8_t)(1U << file);

        if (present != 0U)
        {
            physicalPresence[rank] = (uint8_t)(physicalPresence[rank] | mask);
        }
        else
        {
            physicalPresence[rank] = (uint8_t)(physicalPresence[rank] & ((uint8_t)(~mask)));
        }
    }
}

static bool physicalSquareToVirtualIndex(const char square[APP_SQUARE_TEXT_LEN], uint8_t * row, uint8_t * col)
{
    uint8_t directRow;
    uint8_t directCol;

    if ((square == NULL) || (row == NULL) || (col == NULL))
    {
        return false;
    }

    if (chess_square_to_index(square, &directRow, &directCol) == false)
    {
        return false;
    }

    if ((orientationKnown != 0U) && (orientationFlipRanks != 0U))
    {
        directRow = (uint8_t)(7U - directRow);
    }

    *row = directRow;
    *col = directCol;

    return true;
}

static void virtualIndexToPhysicalSquare(uint8_t row, uint8_t col, char square[APP_SQUARE_TEXT_LEN])
{
    uint8_t physicalRow = row;
    uint8_t physicalCol = col;

    if (square == NULL)
    {
        return;
    }

    if (physicalRow >= 8U)
    {
        physicalRow = 7U;
    }

    if (physicalCol >= 8U)
    {
        physicalCol = 7U;
    }

    if ((orientationKnown != 0U) && (orientationFlipRanks != 0U))
    {
        physicalRow = (uint8_t)(7U - physicalRow);
    }

    chess_index_to_square(physicalRow, physicalCol, square);
}

static void setVirtualBit(uint8_t map[APP_BOARD_RANK_COUNT], uint8_t row, uint8_t col)
{
    char physicalSquare[APP_SQUARE_TEXT_LEN];

    if (map == NULL)
    {
        return;
    }

    virtualIndexToPhysicalSquare(row, col, physicalSquare);
    setBit(map, physicalSquare);
}

static bool selectOrientationForLift(const char square[APP_SQUARE_TEXT_LEN], uint8_t * row, uint8_t * col, chess_piece_t * piece)
{
    uint8_t directRow;
    uint8_t directCol;
    uint8_t flippedRow;
    chess_piece_t directPiece;
    chess_piece_t flippedPiece;

    if ((square == NULL) || (row == NULL) || (col == NULL) || (piece == NULL))
    {
        return false;
    }

    if (chess_square_to_index(square, &directRow, &directCol) == false)
    {
        return false;
    }

    if (orientationKnown != 0U)
    {
        *row = directRow;
        *col = directCol;

        if (orientationFlipRanks != 0U)
        {
            *row = (uint8_t)(7U - directRow);
        }

        *piece = chess_get_piece(&game, *row, *col);
        return ((piece->type != CHESS_EMPTY) && (piece->color == game.side_to_move));
    }

    directPiece = chess_get_piece(&game, directRow, directCol);
    flippedRow = (uint8_t)(7U - directRow);
    flippedPiece = chess_get_piece(&game, flippedRow, directCol);

    if ((directPiece.type != CHESS_EMPTY) && (directPiece.color == game.side_to_move))
    {
        orientationKnown = 1U;
        orientationFlipRanks = 0U;
        *row = directRow;
        *col = directCol;
        *piece = directPiece;
        ESP_LOGI(TAG, "Board orientation selected: normal, first moved side is white");
        return true;
    }

    if ((flippedPiece.type != CHESS_EMPTY) && (flippedPiece.color == game.side_to_move))
    {
        orientationKnown = 1U;
        orientationFlipRanks = 1U;
        *row = flippedRow;
        *col = directCol;
        *piece = flippedPiece;
        ESP_LOGI(TAG, "Board orientation selected: flipped ranks, first moved side is white");
        return true;
    }

    *row = directRow;
    *col = directCol;
    *piece = directPiece;

    return false;
}


static void rebuildOwnerMaps(led_command_t * command)
{
    if (command == NULL)
    {
        return;
    }

    if (orientationKnown == 0U)
    {
        return;
    }

    for (uint8_t row = 0U; row < 8U; row++)
    {
        for (uint8_t col = 0U; col < 8U; col++)
        {
            chess_piece_t piece = chess_get_piece(&game, row, col);
            char physicalSquare[APP_SQUARE_TEXT_LEN];
            uint8_t rank;
            uint8_t file;
            uint8_t bit;

            if (piece.type == CHESS_EMPTY)
            {
                continue;
            }

            virtualIndexToPhysicalSquare(row, col, physicalSquare);
            rank = (uint8_t)(physicalSquare[1] - '1');
            file = (uint8_t)(physicalSquare[0] - 'a');
            bit = (uint8_t)(1U << file);

            if (piece.color == CHESS_WHITE)
            {
                command->whitePieces[rank] = (uint8_t)(command->whitePieces[rank] | bit);
            }
            else if (piece.color == CHESS_BLACK)
            {
                command->blackPieces[rank] = (uint8_t)(command->blackPieces[rank] | bit);
            }
        }
    }
}


static void sendLedFrame(uint8_t rainbowActive)
{
    led_command_t command;

    if (gameMode == GAME_MODE_SETUP)
    {
        updateSetupFeedback();
    }

    (void)memset(&command, 0, sizeof(command));
    command.sequence = ledSequence++;
    command.gameMode = (uint8_t)gameMode;
    command.sideToMove = ((orientationKnown != 0U) ? ((game.side_to_move == CHESS_WHITE) ? LED_SIDE_WHITE : LED_SIDE_BLACK) : LED_SIDE_NONE);
    command.rainbowActive = rainbowActive;
    command.blinkActive = selectedValid;
    command.mateActive = (gameMode == GAME_MODE_CHECKMATE_LOCK) ? 1U : 0U;
    command.winnerWhite = winnerWhite;

    (void)memcpy(command.physical, physicalPresence, sizeof(command.physical));
    (void)memcpy(command.legal, legalMap, sizeof(command.legal));
    (void)memcpy(command.best, bestMap, sizeof(command.best));
    (void)memcpy(command.invalid, invalidMap, sizeof(command.invalid));
    (void)memcpy(command.check, checkMap, sizeof(command.check));
    (void)snprintf(command.blinkSquare, sizeof(command.blinkSquare), "%s", selectedSquare);


    /* DRAW_LOCK_LED_FRAME */
    if (gameMode == GAME_MODE_DRAW_LOCK)
    {
        for (uint8_t rank = 0U; rank < 8U; rank++)
        {
            command.legal[rank] = 0xFFU;
            command.best[rank] = 0U;
            command.invalid[rank] = 0U;
            command.check[rank] = 0U;
        }

        command.blinkActive = 0U;
        command.mateActive = 0U;
        command.rainbowActive = 0U;
    }

    rebuildOwnerMaps(&command);

    if (ledQueueRef != NULL)
    {
        (void)xQueueSend(ledQueueRef, &command, pdMS_TO_TICKS(20U));
    }
}


static bool physicalMatchesStart(void)
{
    for (uint8_t rank = 0U; rank < 8U; rank++)
    {
        uint8_t expected = ((rank <= 1U) || (rank >= 6U)) ? 0xFFU : 0x00U;
        if (physicalPresence[rank] != expected)
        {
            return false;
        }
    }
    return true;
}

static void updateSetupFeedback(void)
{
    uint32_t missingCount = 0U;
    uint32_t extraCount = 0U;

    clearMaps();

    for (uint8_t rank = 0U; rank < 8U; rank++)
    {
        uint8_t expected = ((rank <= 1U) || (rank >= 6U)) ? 0xFFU : 0x00U;
        uint8_t missing = (uint8_t)(expected & ((uint8_t)(~physicalPresence[rank])));
        uint8_t extra = (uint8_t)(((uint8_t)(~expected)) & physicalPresence[rank]);
        uint8_t wrong = (uint8_t)(missing | extra);

        invalidMap[rank] = wrong;

        for (uint8_t file = 0U; file < 8U; file++)
        {
            uint8_t bit = (uint8_t)(1U << file);
            if ((missing & bit) != 0U)
            {
                missingCount++;
            }
            if ((extra & bit) != 0U)
            {
                extraCount++;
            }
        }
    }

    if ((missingCount == 0U) && (extraCount == 0U))
    {
        (void)snprintf(stateText, sizeof(stateText), "SETUP_READY");
        (void)snprintf(stateLegal, sizeof(stateLegal), "Setup ready. Press Start.");
    }
    else
    {
        (void)snprintf(stateText, sizeof(stateText), "SETUP_NOT_READY");
        (void)snprintf(stateLegal, sizeof(stateLegal), "Missing:%lu Extra:%lu", (unsigned long)missingCount, (unsigned long)extraCount);
    }

    (void)snprintf(stateBest, sizeof(stateBest), "-----");
}


static bool physicalMatchesBoard(void)
{
    uint8_t expected[8] = {0U};

    for (uint8_t row = 0U; row < 8U; row++)
    {
        for (uint8_t col = 0U; col < 8U; col++)
        {
            if (game.board[row][col].type != CHESS_EMPTY)
            {
                char physicalSquare[APP_SQUARE_TEXT_LEN];
                uint8_t rank;
                uint8_t file;

                virtualIndexToPhysicalSquare(row, col, physicalSquare);
                rank = (uint8_t)(physicalSquare[1] - '1');
                file = (uint8_t)(physicalSquare[0] - 'a');
                expected[rank] = (uint8_t)(expected[rank] | ((uint8_t)(1U << file)));
            }
        }
    }

    for (uint8_t rank = 0U; rank < 8U; rank++)
    {
        if (physicalPresence[rank] != expected[rank])
        {
            return false;
        }
    }

    return true;
}


static void updateLegalTextFromMap(void)
{
    size_t pos = 0U;
    bool any = false;

    for (uint8_t rank = 0U; rank < 8U; rank++)
    {
        for (uint8_t file = 0U; file < 8U; file++)
        {
            if ((legalMap[rank] & ((uint8_t)(1U << file))) != 0U)
            {
                if (pos < (sizeof(stateLegal) - 4U))
                {
                    pos += (size_t)snprintf(&stateLegal[pos], sizeof(stateLegal) - pos, "%c%c ", (char)('a' + file), (char)('1' + rank));
                    any = true;
                }
            }
        }
    }

    if (any == false)
    {
        (void)snprintf(stateLegal, sizeof(stateLegal), "-");
    }
}

static void updateCheckState(void)
{
    uint8_t row;
    uint8_t col;

    (void)memset(checkMap, 0, sizeof(checkMap));

    if ((chess_is_check(&game, game.side_to_move) == true) &&
        (chess_find_king(&game, game.side_to_move, &row, &col) == true))
    {
        setVirtualBit(checkMap, row, col);

        if (chess_is_checkmate(&game, game.side_to_move) == true)
        {
            winnerWhite = (game.side_to_move == CHESS_BLACK) ? 1U : 0U;
            setMode(GAME_MODE_CHECKMATE_LOCK, "CHECKMATE");
        }
    }
}



static void updatePostMoveTerminalState(void)
{
    chess_move_t moves[CHESS_MAX_MOVES];
    uint32_t moveCount;
    bool inCheck;

    /* POST_MOVE_STALEMATE_CHECK */
    updateCheckState();

    if (gameMode == GAME_MODE_CHECKMATE_LOCK)
    {
        return;
    }

    moveCount = chess_generate_all_legal_moves(&game, moves, CHESS_MAX_MOVES);
    inCheck = chess_is_check(&game, game.side_to_move);

    if (moveCount == 0U)
    {
        if (inCheck != false)
        {
            winnerWhite = (game.side_to_move == CHESS_BLACK) ? 1U : 0U;
            setMode(GAME_MODE_CHECKMATE_LOCK, "CHECKMATE");
        }
        else
        {
            declareDraw();
            setMode(GAME_MODE_DRAW_LOCK, "STALEMATE");
        }

        return;
    }

    if (inCheck != false)
    {
        setMode(GAME_MODE_RUNNING, "CHECK");
    }
    else
    {
        setMode(GAME_MODE_RUNNING, "RUNNING");
    }
}


static void registerInfraction(const char * reason)
{
    uint8_t * counter;
    const char * sideText;

    counter = (game.side_to_move == CHESS_WHITE) ? &whiteInfractions : &blackInfractions;
    sideText = (game.side_to_move == CHESS_WHITE) ? "white" : "black";

    if (runtimeConfigGet()->invalid_position_infractions_enabled == 0U)
    {
        ESP_LOGW(TAG, "INFRACTION_DISABLED side=%s reason=%s", sideText, (reason != NULL) ? reason : "invalid move");
        return;
    }

    if (*counter < 2U)
    {
        (*counter)++;
    }

    ESP_LOGW(TAG, "Infraction %u/2 by %s side: %s", (unsigned int)(*counter), sideText, (reason != NULL) ? reason : "invalid move");

    if (*counter >= 2U)
    {
        clearMaps();
        (void)memset(checkMap, 0, sizeof(checkMap));
        winnerWhite = (game.side_to_move == CHESS_BLACK) ? 1U : 0U;
        selectedValid = 0U;
        selectedSquare[0] = '\0';
        pendingPromotionValid = 0U;
        setMode(GAME_MODE_CHECKMATE_LOCK, "INFRACTION_LOSS");
    }
}




static int pieceMaterialValue(chess_piece_type_t type)
{
    int value = 0;

    switch (type)
    {
        case CHESS_PAWN:
            value = 1;
            break;

        case CHESS_KNIGHT:
            value = 3;
            break;

        case CHESS_BISHOP:
            value = 3;
            break;

        case CHESS_ROOK:
            value = 5;
            break;

        case CHESS_QUEEN:
            value = 9;
            break;

        case CHESS_KING:
        case CHESS_EMPTY:
        default:
            value = 0;
            break;
    }

    return value;
}

static char pieceMaterialLetter(chess_piece_type_t type)
{
    char value = '-';

    switch (type)
    {
        case CHESS_PAWN:
            value = 'P';
            break;

        case CHESS_KNIGHT:
            value = 'N';
            break;

        case CHESS_BISHOP:
            value = 'B';
            break;

        case CHESS_ROOK:
            value = 'R';
            break;

        case CHESS_QUEEN:
            value = 'Q';
            break;

        case CHESS_KING:
        case CHESS_EMPTY:
        default:
            value = '-';
            break;
    }

    return value;
}

static void clearCapturedMaterial(void)
{
    (void)memset(capturedByWhiteCounts, 0, sizeof(capturedByWhiteCounts));
    (void)memset(capturedByBlackCounts, 0, sizeof(capturedByBlackCounts));
}

static void recordCapturedPiece(chess_color_t capturingSide, chess_piece_t capturedPiece)
{
    uint8_t * capturedCounts = NULL;

    if ((capturedPiece.type == CHESS_EMPTY) ||
        (capturedPiece.type == CHESS_KING) ||
        ((uint32_t)capturedPiece.type >= CAPTURED_PIECE_SLOT_COUNT))
    {
        return;
    }

    if (capturingSide == CHESS_WHITE)
    {
        capturedCounts = capturedByWhiteCounts;
    }
    else if (capturingSide == CHESS_BLACK)
    {
        capturedCounts = capturedByBlackCounts;
    }

    if (capturedCounts != NULL)
    {
        if (capturedCounts[capturedPiece.type] < UINT8_MAX)
        {
            capturedCounts[capturedPiece.type]++;
        }
    }
}

static uint8_t capturedCountForSide(chess_color_t capturingSide, chess_piece_type_t type)
{
    uint8_t count = 0U;

    if ((uint32_t)type < CAPTURED_PIECE_SLOT_COUNT)
    {
        if (capturingSide == CHESS_WHITE)
        {
            count = capturedByWhiteCounts[type];
        }
        else if (capturingSide == CHESS_BLACK)
        {
            count = capturedByBlackCounts[type];
        }
    }

    return count;
}

static void appendCaptureText(char * dst, size_t dstLen, size_t * pos, const char * text)
{
    if ((dst == NULL) || (pos == NULL) || (*pos >= dstLen))
    {
        return;
    }

    *pos += (size_t)snprintf(&dst[*pos], dstLen - *pos, "%s", text);
}

static void buildCapturedMaterialText(chess_color_t capturingSide, char * dst, size_t dstLen, uint16_t * points)
{
    static const chess_piece_type_t order[5] = {
        CHESS_QUEEN, CHESS_ROOK, CHESS_BISHOP, CHESS_KNIGHT, CHESS_PAWN
    };
    size_t pos = 0U;
    bool any = false;
    uint16_t score = 0U;

    if ((dst == NULL) || (dstLen == 0U) || (points == NULL))
    {
        return;
    }

    dst[0] = '\0';

    for (uint8_t index = 0U; index < 5U; index++)
    {
        chess_piece_type_t type = order[index];
        uint8_t captured = capturedCountForSide(capturingSide, type);

        if (captured != 0U)
        {
            char item[16];

            if (any != false)
            {
                appendCaptureText(dst, dstLen, &pos, " ");
            }

            (void)snprintf(item, sizeof(item), "%cx%u", pieceMaterialLetter(type), (unsigned int)captured);
            appendCaptureText(dst, dstLen, &pos, item);
            score = (uint16_t)(score + ((uint16_t)captured * (uint16_t)pieceMaterialValue(type)));
            any = true;
        }
    }

    if (any == false)
    {
        (void)snprintf(dst, dstLen, "none");
    }

    *points = score;
}



static bool isStockfishMoveText(const char * text)
{
    bool ok = false;

    if (text != NULL)
    {
        ok = ((text[0] >= 'a') && (text[0] <= 'h') &&
              (text[1] >= '1') && (text[1] <= '8') &&
              (text[2] >= 'a') && (text[2] <= 'h') &&
              (text[3] >= '1') && (text[3] <= '8'));
    }

    return ok;
}

static void clearStockfishBest(void)
{
    stockfishBestValid = 0U;
    stockfishBestFen[0] = '\0';
    stockfishBestMove[0] = '\0';
    stateStockfishJson[0] = '\0';
    (void)memset(bestMap, 0, sizeof(bestMap));
    (void)snprintf(stateBest, sizeof(stateBest), "-----");
}
static void clearPendingStockfishQueues(void)
{
    stockfish_request_t request;
    stockfish_response_t response;

    while ((stockfishRequestQueue != NULL) &&
           (xQueueReceive(stockfishRequestQueue, &request, 0) == pdPASS))
    {
    }

    while ((stockfishResponseQueue != NULL) &&
           (xQueueReceive(stockfishResponseQueue, &response, 0) == pdPASS))
    {
    }
}

static void disableStockfishAdvisorNow(void)
{
    stockfishRequestSequence++;
    clearPendingStockfishQueues();
    clearStockfishBest();
    sendLedFrame(0U);
}

static void refreshStockfishAdvisorAfterConfig(uint8_t wasEnabled)
{
    uint8_t isEnabled = runtimeConfigGet()->stockfish_enabled;

    if (isEnabled == 0U)
    {
        disableStockfishAdvisorNow();
        return;
    }

    if ((wasEnabled == 0U) && (gameMode == GAME_MODE_RUNNING))
    {
        clearPendingStockfishQueues();
        clearStockfishBest();
        requestStockfishBestAsync();
        sendLedFrame(0U);
        return;
    }

    sendLedFrame(0U);
}




static void refreshStockfishBestMap(void)
{
    char from[APP_SQUARE_TEXT_LEN];
    char to[APP_SQUARE_TEXT_LEN];
    uint8_t fromRow;
    uint8_t fromCol;
    uint8_t toRow;
    uint8_t toCol;

    (void)memset(bestMap, 0, sizeof(bestMap));

    if ((stockfishBestValid == 0U) ||
        (strcmp(stockfishBestFen, game.fen) != 0) ||
        (isStockfishMoveText(stockfishBestMove) == false))
    {
        (void)snprintf(stateBest, sizeof(stateBest), "-----");
        return;
    }

    from[0] = stockfishBestMove[0];
    from[1] = stockfishBestMove[1];
    from[2] = '\0';

    to[0] = stockfishBestMove[2];
    to[1] = stockfishBestMove[3];
    to[2] = '\0';

    if ((chess_square_to_index(from, &fromRow, &fromCol) == false) ||
        (chess_square_to_index(to, &toRow, &toCol) == false))
    {
        clearStockfishBest();
        return;
    }

    (void)snprintf(stateBest, sizeof(stateBest), "%s", stockfishBestMove);

    if (orientationKnown == 0U)
    {
        return;
    }

    setVirtualBit(bestMap, fromRow, fromCol);
    setVirtualBit(bestMap, toRow, toCol);
}

static void requestStockfishBestAsync(void)
{
    stockfish_request_t request;

    if ((stockfishRequestQueue == NULL) || (gameMode != GAME_MODE_RUNNING))
    {
        return;
    }

    if (runtimeConfigGet()->stockfish_enabled == 0U)
    {
        disableStockfishAdvisorNow();
        return;
    }

    (void)memset(&request, 0, sizeof(request));

    stockfishRequestSequence++;
    request.requestId = stockfishRequestSequence;
    (void)snprintf(request.fen, sizeof(request.fen), "%s", game.fen);

    clearStockfishBest();

    (void)xQueueOverwrite(stockfishRequestQueue, &request);

    ESP_LOGI(
        TAG,
        "BEST_REQUEST id=%lu fen=%s",
        (unsigned long)request.requestId,
        request.fen
    );
}

static void processStockfishResponses(void)
{
    stockfish_response_t response;

    if (runtimeConfigGet()->stockfish_enabled == 0U)
    {
        /* STOCKFISH_DISABLED_DROP_RESPONSES */
        disableStockfishAdvisorNow();
        return;
    }

    while ((stockfishResponseQueue != NULL) &&
           (xQueueReceive(stockfishResponseQueue, &response, 0) == pdPASS))
    {
        if ((response.requestId != stockfishRequestSequence) ||
            (strcmp(response.fen, game.fen) != 0))
        {
            ESP_LOGI(
                TAG,
                "BEST_DROP stale id=%lu current=%lu",
                (unsigned long)response.requestId,
                (unsigned long)stockfishRequestSequence
            );
            continue;
        }

        if ((response.result.valid == 0U) ||
            (isStockfishMoveText(response.result.bestMove) == false))
        {
            clearStockfishBest();

            ESP_LOGW(
                TAG,
                "BEST_UNAVAILABLE id=%lu fen=%s",
                (unsigned long)response.requestId,
                game.fen
            );

            sendLedFrame(0U);
            continue;
        }

        stockfishBestValid = 1U;
        (void)snprintf(stockfishBestFen, sizeof(stockfishBestFen), "%s", response.fen);
        (void)snprintf(stockfishBestMove, sizeof(stockfishBestMove), "%s", response.result.bestMove);
        (void)snprintf(stateStockfishJson, sizeof(stateStockfishJson), "%s", response.result.json);

        refreshStockfishBestMap();

        ESP_LOGI(
            TAG,
            "BEST_HINT id=%lu move=%s fen=%s",
            (unsigned long)response.requestId,
            stockfishBestMove,
            game.fen
        );

        sendLedFrame(0U);
    }
}

static void stockfishTask(void * parameters)
{
    stockfish_request_t request;
    stockfish_request_t latest;
    stockfish_response_t response;

    (void)parameters;

    ESP_LOGI(TAG, "StockfishOnline advisor task started");

    for (;;)
    {
        if (stockfishRequestQueue == NULL)
        {
            vTaskDelay(pdMS_TO_TICKS(1000U));
            continue;
        }

        if (xQueueReceive(stockfishRequestQueue, &request, portMAX_DELAY) == pdPASS)
        {
            while (xQueueReceive(stockfishRequestQueue, &latest, 0) == pdPASS)
            {
                request = latest;
            }

            (void)memset(&response, 0, sizeof(response));
            response.requestId = request.requestId;
            (void)snprintf(response.fen, sizeof(response.fen), "%s", request.fen);

            (void)stockfishClientAnalyzeFen(request.fen, &response.result);

            if (stockfishResponseQueue != NULL)
            {
                (void)xQueueOverwrite(stockfishResponseQueue, &response);
            }
        }
    }
}

static void computeLiftHints(uint8_t row, uint8_t col)
{
    chess_move_t moves[CHESS_MAX_MOVES];
    uint32_t count;

    clearMaps();

    count = chess_generate_legal_moves_from(&game, row, col, moves, CHESS_MAX_MOVES);

    for (uint32_t i = 0U; i < count; i++)
    {
        setVirtualBit(legalMap, moves[i].to_row, moves[i].to_col);
    }

    updateLegalTextFromMap();
    refreshStockfishBestMap();
}


static chess_piece_type_t selectorPromotionPiece(const char square[APP_SQUARE_TEXT_LEN])
{
    if (square == NULL)
    {
        return CHESS_EMPTY;
    }

    if ((square[1] == '1') || (square[1] == '8'))
    {
        if ((square[0] == 'a') || (square[0] == 'h')) return CHESS_ROOK;
        if ((square[0] == 'b') || (square[0] == 'g')) return CHESS_KNIGHT;
        if ((square[0] == 'c') || (square[0] == 'f')) return CHESS_BISHOP;
        if (square[0] == 'd') return CHESS_QUEEN;
    }

    return CHESS_EMPTY;
}

static bool applySelectedMove(uint8_t toRow, uint8_t toCol, chess_piece_type_t promotion)
{
    uint8_t fromRow;
    uint8_t fromCol;
    chess_move_t move;
    char fromText[APP_SQUARE_TEXT_LEN];
    char toText[APP_SQUARE_TEXT_LEN];
    char toPhysical[APP_SQUARE_TEXT_LEN];
    chess_piece_t movingPiece;
    chess_piece_t targetPiece;
    chess_piece_t capturedPiece;

    if ((selectedValid == 0U) || (physicalSquareToVirtualIndex(selectedSquare, &fromRow, &fromCol) == false))
    {
        return false;
    }

    move.from_row = fromRow;
    move.from_col = fromCol;
    move.to_row = toRow;
    move.to_col = toCol;
    move.promotion = promotion;

    indexToText(fromRow, fromCol, fromText);
    indexToText(toRow, toCol, toText);
    virtualIndexToPhysicalSquare(toRow, toCol, toPhysical);
    movingPiece = chess_get_piece(&game, fromRow, fromCol);
    targetPiece = chess_get_piece(&game, toRow, toCol);
    capturedPiece = targetPiece;

    if ((capturedPiece.type == CHESS_EMPTY) &&
        (movingPiece.type == CHESS_PAWN) &&
        (fromCol != toCol) &&
        (game.en_passant_row == (int8_t)toRow) &&
        (game.en_passant_col == (int8_t)toCol))
    {
        uint8_t capturedRow = toRow;

        if ((movingPiece.color == CHESS_WHITE) && (toRow > 0U))
        {
            capturedRow = (uint8_t)(toRow - 1U);
        }
        else if ((movingPiece.color == CHESS_BLACK) && (toRow < 7U))
        {
            capturedRow = (uint8_t)(toRow + 1U);
        }

        capturedPiece = chess_get_piece(&game, capturedRow, toCol);
    }

    ESP_LOGI(
        TAG,
        "MOVE_TRY physical=%s->%s virtual=%s->%s side=%s piece=%s target=%s/%s promotion=%s",
        selectedSquare,
        toPhysical,
        fromText,
        toText,
        pieceColorText(game.side_to_move),
        pieceTypeText(movingPiece.type),
        pieceColorText(targetPiece.color),
        pieceTypeText(targetPiece.type),
        pieceTypeText(promotion)
    );

    /* CLOCK_UPDATE_BEFORE_MOVE_APPLY */
    clockUpdate();
    if (gameMode == GAME_MODE_CHECKMATE_LOCK)
    {
        return true;
    }

    if (chess_is_legal_move(&game, &move) == false)
    {
        ESP_LOGW(TAG, "MOVE_REJECT reason=RULE_ENGINE physical=%s->%s virtual=%s->%s fen=%s", selectedSquare, toPhysical, fromText, toText, game.fen);
        return false;
    }

    if ((promotion == CHESS_EMPTY) && (chess_is_promotion_move(&game, &move) == true))
    {
        pendingPromotionMove = move;
        pendingPromotionValid = 1U;
        setMode(GAME_MODE_PROMOTION_PENDING, "PROMOTION_PENDING");
        clearMaps();
        virtualIndexToPhysicalSquare(toRow, toCol, selectedSquare);
        selectedValid = 1U;
        return true;
    }

    if (promotion == CHESS_EMPTY)
    {
        move.promotion = CHESS_QUEEN;
    }

    if (chess_apply_move(&game, &move) == false)
    {
        ESP_LOGE(TAG, "MOVE_REJECT reason=APPLY_FAILED physical=%s->%s virtual=%s->%s fen=%s", selectedSquare, toPhysical, fromText, toText, game.fen);
        return false;
    }

    recordCapturedPiece(movingPiece.color, capturedPiece);
    /* CLOCK_BONUS_AFTER_MOVE */
    clockApplyIncrement(movingPiece.color);
    ESP_LOGI(TAG, "MOVE_OK physical=%s->%s virtual=%s->%s captured=%s/%s fen=%s pgn=%s next=%s", selectedSquare, toPhysical, fromText, toText, pieceColorText(capturedPiece.color), pieceTypeText(capturedPiece.type), game.fen, game.pgn, pieceColorText(game.side_to_move));
    selectedValid = 0U;
    selectedSquare[0] = '\0';
    pendingPromotionValid = 0U;
    clearMaps();

    /* DRAW_OFFER_CLEAR_ON_MOVE */
    drawOfferActive = 0U;
    drawOfferByWhite = 0U;

    updatePostMoveTerminalState();

    if (gameMode == GAME_MODE_RUNNING)
    {
        /* BEST_REQUEST_AFTER_MOVE */
        requestStockfishBestAsync();
    }

    return true;
}



static void startGame(void)
{
    clearMaps();
    selectedValid = 0U;
    selectedSquare[0] = '\0';
    pendingPromotionValid = 0U;
    winnerWhite = 0U;
    whiteInfractions = 0U;
    blackInfractions = 0U;
    /* DRAW_OFFER_CLEAR_ON_START */
    drawOfferActive = 0U;
    drawOfferByWhite = 0U;
    clearCapturedMaterial();
    orientationKnown = 0U;
    orientationFlipRanks = 0U;

    if (physicalMatchesStart() == false)
    {
        setMode(GAME_MODE_SETUP, "SETUP_NOT_READY");
        updateSetupFeedback();
        sendLedFrame(0U);
        return;
    }

    chess_reset(&game);
    /* CLOCK_RESET_ON_START */
    clockResetForNewGame();
    /* STOCKFISH_DEFAULT_ORIENTATION_ON_START */
    orientationKnown = 1U;
    orientationFlipRanks = 0U;
    setMode(GAME_MODE_RUNNING, "RUNNING_WAIT_FIRST_WHITE");
    /* CLOCK_UPDATE_ON_START_ZERO */
    clockUpdate();

    if (gameMode == GAME_MODE_RUNNING)
    {
        /* BEST_REQUEST_AFTER_START */
        requestStockfishBestAsync();
    }
    sendLedFrame(1U);
    vTaskDelay(pdMS_TO_TICKS(START_RAINBOW_MS));
    sendLedFrame(0U);
}


static uint32_t clockNowMs(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static uint32_t clockSecondsToMs(uint16_t seconds)
{
    return ((uint32_t)seconds * 1000U);
}

static uint32_t clockAddMs(uint32_t currentMs, uint32_t addMs)
{
    uint32_t value = currentMs;

    if ((UINT32_MAX - value) < addMs)
    {
        value = UINT32_MAX;
    }
    else
    {
        value += addMs;
    }

    return value;
}

static bool clockModeAllowsCounting(void)
{
    bool active = false;

    if ((gameMode == GAME_MODE_RUNNING) ||
        (gameMode == GAME_MODE_LIFTED) ||
        (gameMode == GAME_MODE_PROMOTION_PENDING) ||
        (gameMode == GAME_MODE_INVALID_LOCK) ||
        (gameMode == GAME_MODE_SYNC_WAIT))
    {
        active = true;
    }

    return active;
}

static void clockResetForNewGame(void)
{
    uint32_t initialMs;

    initialMs = clockSecondsToMs(runtimeConfigGet()->clock_initial_seconds);
    whiteClockMs = initialMs;
    blackClockMs = initialMs;
    clockLastTickMs = clockNowMs();
    clockActive = 1U;
}

static void declareTimeLoss(chess_color_t losingSide)
{
    clearMaps();
    selectedValid = 0U;
    selectedSquare[0] = '\0';
    pendingPromotionValid = 0U;
    drawOfferActive = 0U;
    drawOfferByWhite = 0U;
    clockActive = 0U;

    winnerWhite = (losingSide == CHESS_BLACK) ? 1U : 0U;

    if (losingSide == CHESS_WHITE)
    {
        setMode(GAME_MODE_CHECKMATE_LOCK, "WHITE_TIME_LOSS");
    }
    else
    {
        setMode(GAME_MODE_CHECKMATE_LOCK, "BLACK_TIME_LOSS");
    }

    ESP_LOGW(TAG, "CLOCK_LOSS side=%s", pieceColorText(losingSide));
    sendLedFrame(0U);
}

static void clockUpdate(void)
{
    uint32_t nowMs;
    uint32_t elapsedMs;
    uint32_t * activeClock = NULL;
    chess_color_t activeSide = game.side_to_move;

    if (clockActive == 0U)
    {
        return;
    }

    nowMs = clockNowMs();

    if (clockModeAllowsCounting() == false)
    {
        clockLastTickMs = nowMs;
        return;
    }

    if (activeSide == CHESS_WHITE)
    {
        activeClock = &whiteClockMs;
    }
    else if (activeSide == CHESS_BLACK)
    {
        activeClock = &blackClockMs;
    }

    if (activeClock == NULL)
    {
        clockLastTickMs = nowMs;
        return;
    }

    if (*activeClock == 0U)
    {
        declareTimeLoss(activeSide);
        return;
    }

    elapsedMs = nowMs - clockLastTickMs;
    clockLastTickMs = nowMs;

    if (elapsedMs >= *activeClock)
    {
        *activeClock = 0U;
        declareTimeLoss(activeSide);
    }
    else
    {
        *activeClock -= elapsedMs;
    }
}

static void clockApplyIncrement(chess_color_t side)
{
    uint32_t bonusMs;

    if (clockActive == 0U)
    {
        return;
    }

    bonusMs = clockSecondsToMs(runtimeConfigGet()->clock_bonus_seconds);

    if (side == CHESS_WHITE)
    {
        whiteClockMs = clockAddMs(whiteClockMs, bonusMs);
    }
    else if (side == CHESS_BLACK)
    {
        blackClockMs = clockAddMs(blackClockMs, bonusMs);
    }

    clockLastTickMs = clockNowMs();
}


static void declareDraw(void)
{
    clearMaps();

    for (uint8_t rank = 0U; rank < 8U; rank++)
    {
        legalMap[rank] = 0xFFU;
    }

    selectedValid = 0U;
    selectedSquare[0] = '\0';
    pendingPromotionValid = 0U;
    winnerWhite = 0U;
    setMode(GAME_MODE_DRAW_LOCK, "DRAW");
    sendLedFrame(0U);
}



static void handleGameCommand(const game_command_t * command)
{
    if (command == NULL)
    {
        return;
    }

    if (command->type == GAME_COMMAND_START)
    {
        startGame();
    }
    else if ((command->type == GAME_COMMAND_PROMOTE) && (gameMode == GAME_MODE_PROMOTION_PENDING) && (pendingPromotionValid != 0U))
    {
        selectedValid = 1U;
        virtualIndexToPhysicalSquare(pendingPromotionMove.from_row, pendingPromotionMove.from_col, selectedSquare);
        (void)applySelectedMove(pendingPromotionMove.to_row, pendingPromotionMove.to_col, command->promotion);
        sendLedFrame(0U);
    }

    else if (command->type == GAME_COMMAND_DRAW)
    {
        declareDraw();
    }
}


static void processPendingCommands(void)
{
    game_command_t command;

    while ((gameCommandQueue != NULL) && (xQueueReceive(gameCommandQueue, &command, 0) == pdPASS))
    {
        handleGameCommand(&command);
    }
}

static void handleSensorEvent(const sensor_event_t * event)
{
    uint8_t row = 0U;
    uint8_t col = 0U;
    uint8_t targetRow = 0U;
    uint8_t targetCol = 0U;
    chess_piece_t piece = { CHESS_EMPTY, CHESS_COLOR_NONE, 0U };
    chess_piece_t targetPiece = { CHESS_EMPTY, CHESS_COLOR_NONE, 0U };
    chess_piece_type_t promotion;

    if ((event == NULL) || (chess_square_to_index(event->square, &row, &col) == false))
    {
        return;
    }

    updatePhysical(event->square, (event->state == SENSOR_STATE_PRESENT) ? 1U : 0U);
    logBoardEvent(event);

    if (gameMode == GAME_MODE_SETUP)
    {
        updateSetupFeedback();
        sendLedFrame(0U);
        return;
    }

    if ((gameMode == GAME_MODE_CHECKMATE_LOCK) || (gameMode == GAME_MODE_DRAW_LOCK))
    {
        sendLedFrame(0U);
        return;
    }

    if (gameMode == GAME_MODE_INVALID_LOCK)
    {
        if ((selectedValid != 0U) &&
            (event->state == SENSOR_STATE_PRESENT) &&
            (strcmp(event->square, selectedSquare) == 0))
        {
            ESP_LOGI(TAG, "INVALID_RESTORED origin=%s", selectedSquare);
            clearMaps();
            selectedValid = 0U;
            selectedSquare[0] = '\0';

            if (chess_is_check(&game, game.side_to_move) == true)
            {
                updateCheckState();
                if (gameMode != GAME_MODE_CHECKMATE_LOCK)
                {
                    setMode(GAME_MODE_RUNNING, "CHECK");
                }
            }
            else
            {
                setMode(GAME_MODE_RUNNING, "RUNNING");
            }
        }
        else if (physicalMatchesBoard() == true)
        {
            ESP_LOGI(TAG, "INVALID_RESTORED full_board_match");
            clearMaps();
            selectedValid = 0U;
            selectedSquare[0] = '\0';
            setMode(GAME_MODE_RUNNING, "RUNNING");
        }
        else
        {
            ESP_LOGW(
                TAG,
                "INVALID_WAIT_RESTORE selected_origin=%s current_event=%s state=%s",
                (selectedValid != 0U) ? selectedSquare : "-",
                event->square,
                sensorStateText(event->state)
            );
        }

        sendLedFrame(0U);
        return;
    }

    if (gameMode == GAME_MODE_SYNC_WAIT)
    {
        clearMaps();
        selectedValid = 0U;
        selectedSquare[0] = '\0';
        setMode(GAME_MODE_RUNNING, "RUNNING_RESYNC");
    }

    if (gameMode == GAME_MODE_PROMOTION_PENDING)
    {
        promotion = selectorPromotionPiece(event->square);

        if ((promotion != CHESS_EMPTY) &&
            (event->state == SENSOR_STATE_PRESENT) &&
            (pendingPromotionValid != 0U))
        {
            game_command_t command = { GAME_COMMAND_PROMOTE, promotion };
            handleGameCommand(&command);
        }

        sendLedFrame(0U);
        return;
    }

    if (event->state == SENSOR_STATE_LIFTED)
    {
        if ((selectedValid != 0U) &&
            (orientationKnown != 0U) &&
            (physicalSquareToVirtualIndex(event->square, &targetRow, &targetCol) == true))
        {
            targetPiece = chess_get_piece(&game, targetRow, targetCol);

            if ((targetPiece.type != CHESS_EMPTY) &&
                (targetPiece.color != game.side_to_move))
            {
                clearMaps();
                setVirtualBit(legalMap, targetRow, targetCol);
                setMode(GAME_MODE_LIFTED, "CAPTURE_TARGET_REMOVED");
                ESP_LOGI(TAG, "Capture target removed at %s. Place the selected piece on that square.", event->square);
                sendLedFrame(0U);
                return;
            }
        }

        clearMaps();

        if (selectOrientationForLift(event->square, &row, &col, &piece) == true)
        {
            (void)snprintf(selectedSquare, sizeof(selectedSquare), "%s", event->square);
            selectedValid = 1U;
            computeLiftHints(row, col);
            setMode(GAME_MODE_LIFTED, "PIECE_LIFTED");
        }
        else if ((orientationKnown != 0U) &&
                 (physicalSquareToVirtualIndex(event->square, &targetRow, &targetCol) == true))
        {
            targetPiece = chess_get_piece(&game, targetRow, targetCol);

            if ((targetPiece.type != CHESS_EMPTY) &&
                (targetPiece.color != game.side_to_move))
            {
                selectedValid = 0U;
                selectedSquare[0] = '\0';
                setVirtualBit(legalMap, targetRow, targetCol);
                setMode(GAME_MODE_RUNNING, "CAPTURE_TARGET_REMOVED");
                ESP_LOGI(TAG, "Opponent piece removed first at %s. Now lift the moving piece.", event->square);
            }
            else
            {
                selectedValid = 0U;
                selectedSquare[0] = '\0';
                setBit(invalidMap, event->square);
                setMode(GAME_MODE_SYNC_WAIT, "RESTORE_BOARD");
                ESP_LOGW(TAG, "Lift rejected at %s. Restore the physical board.", event->square);
            }
        }
        else
        {
            selectedValid = 0U;
            selectedSquare[0] = '\0';
            setBit(invalidMap, event->square);
            setMode(GAME_MODE_SYNC_WAIT, "RESTORE_BOARD");
            ESP_LOGW(TAG, "Lift rejected at %s. Restore the physical board.", event->square);
        }
    }
    else if ((event->state == SENSOR_STATE_PRESENT) && (selectedValid != 0U))
    {
        if (strcmp(selectedSquare, event->square) == 0)
        {
            selectedValid = 0U;
            selectedSquare[0] = '\0';
            clearMaps();
            setMode(GAME_MODE_RUNNING, "RUNNING");
        }
        else if (physicalSquareToVirtualIndex(event->square, &row, &col) == false)
        {
            setBit(invalidMap, selectedSquare);
            setBit(invalidMap, event->square);
            registerInfraction("INVALID_SQUARE");

            if (gameMode != GAME_MODE_CHECKMATE_LOCK)
            {
                setMode(GAME_MODE_INVALID_LOCK, "INVALID_SQUARE");
            }
        }
        else if (applySelectedMove(row, col, CHESS_EMPTY) == false)
        {
            setBit(invalidMap, selectedSquare);
            setBit(invalidMap, event->square);
            registerInfraction("INVALID_MOVE");

            if (gameMode != GAME_MODE_CHECKMATE_LOCK)
            {
                setMode(GAME_MODE_INVALID_LOCK, "INVALID_MOVE");
            }
        }
    }

    sendLedFrame(0U);
}



esp_err_t serverInit(QueueHandle_t sensorQueue, QueueHandle_t ledQueue)
{
    esp_err_t err = ESP_ERR_INVALID_ARG;

    if ((sensorQueue != NULL) && (ledQueue != NULL))
    {
        sensorQueueRef = sensorQueue;
        ledQueueRef = ledQueue;
        stateMutex = xSemaphoreCreateMutex();
        gameCommandQueue = xQueueCreateStatic(
            GAME_COMMAND_QUEUE_LEN,
            sizeof(game_command_t),
            gameCommandQueueStorage,
            &gameCommandQueueControl
        );


        stockfishRequestQueue = xQueueCreateStatic(
            STOCKFISH_REQUEST_QUEUE_LEN,
            sizeof(stockfish_request_t),
            stockfishRequestQueueStorage,
            &stockfishRequestQueueControl
        );

        stockfishResponseQueue = xQueueCreateStatic(
            STOCKFISH_RESPONSE_QUEUE_LEN,
            sizeof(stockfish_response_t),
            stockfishResponseQueueStorage,
            &stockfishResponseQueueControl
        );
        if ((stateMutex != NULL) &&
            (gameCommandQueue != NULL) &&
            (stockfishRequestQueue != NULL) &&
            (stockfishResponseQueue != NULL))
        {
            chess_reset(&game);
            setMode(GAME_MODE_SETUP, "SETUP");
            stockfishTaskHandle = xTaskCreateStatic(
                stockfishTask,
                "stockfish_task",
                SERVER_STACK_WORD_COUNT(STOCKFISH_TASK_STACK_BYTES),
                NULL,
                STOCKFISH_TASK_PRIORITY,
                stockfishTaskStack,
                &stockfishTaskControlBlock
            );

            if (stockfishTaskHandle != NULL)
            {
                err = ESP_OK;
            }
            else
            {
                err = ESP_FAIL;
            }
        }
    }

    return err;
}

static esp_err_t initNvs(void)
{
    esp_err_t err = nvs_flash_init();
    if ((err == ESP_ERR_NVS_NO_FREE_PAGES) || (err == ESP_ERR_NVS_NEW_VERSION_FOUND))
    {
        err = nvs_flash_erase();
        if (err == ESP_OK)
        {
            err = nvs_flash_init();
        }
    }
    return err;
}

static esp_err_t initEnterpriseAuth(const wifi_enterprise_credentials_t * credentials)
{
    size_t identity_len;
    size_t username_len;
    size_t password_len;
    esp_err_t err = ESP_ERR_INVALID_ARG;

    if ((credentials == NULL) || (credentials->valid == 0U))
    {
        return ESP_ERR_INVALID_ARG;
    }

    identity_len = boundedStringLength(credentials->identity, WIFI_ENTERPRISE_EAP_MAX_LEN + 1U);
    username_len = boundedStringLength(credentials->username, WIFI_ENTERPRISE_EAP_MAX_LEN + 1U);
    password_len = boundedStringLength(credentials->password, WIFI_ENTERPRISE_EAP_MAX_LEN + 1U);

    if ((identity_len > 0U) && (identity_len <= WIFI_ENTERPRISE_EAP_MAX_LEN) &&
        (username_len > 0U) && (username_len <= WIFI_ENTERPRISE_EAP_MAX_LEN) &&
        (password_len > 0U) && (password_len <= WIFI_ENTERPRISE_EAP_MAX_LEN))
    {
        err = esp_eap_client_set_identity((const unsigned char *)credentials->identity, (int)identity_len);
        if (err == ESP_OK)
        {
            err = esp_eap_client_set_username((const unsigned char *)credentials->username, (int)username_len);
        }
        if (err == ESP_OK)
        {
            err = esp_eap_client_set_password((const unsigned char *)credentials->password, (int)password_len);
        }
        if (err == ESP_OK)
        {
            err = esp_wifi_sta_enterprise_enable();
        }
    }

    return err;
}

static void eventHandler(void * arg, esp_event_base_t base, int32_t id, void * data)
{
    esp_err_t err;
    (void)arg;

    if ((base == WIFI_EVENT) && (id == WIFI_EVENT_STA_START))
    {
        if (enterpriseCredentialsReady != 0U)
        {
            err = esp_wifi_connect();
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "WiFi connect error: %ld", (long)err);
            }
        }
        else
        {
            ESP_LOGW(TAG, "WPA2 Enterprise STA credentials missing. SoftAP remains active.");
        }
    }
    else if ((base == WIFI_EVENT) && (id == WIFI_EVENT_STA_DISCONNECTED))
    {
        wifiConnected = 0U;
        if (data != NULL)
        {
            const wifi_event_sta_disconnected_t * const event = (const wifi_event_sta_disconnected_t *)data;
            ESP_LOGE(TAG, "WiFi disconnected. Reason: %u", (unsigned int)event->reason);
        }
        if (enterpriseCredentialsReady != 0U)
        {
            err = esp_wifi_connect();
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "WiFi reconnect error: %ld", (long)err);
            }
        }
    }
    else if ((base == IP_EVENT) && (id == IP_EVENT_STA_GOT_IP))
    {
        wifiConnected = 1U;
        if (data != NULL)
        {
            const ip_event_got_ip_t * const event = (const ip_event_got_ip_t *)data;
            ESP_LOGI(TAG, "ESP32 IP: " IPSTR, IP2STR(&event->ip_info.ip));
        }
    }
}

esp_err_t serverNetworkInit(void)
{
    esp_err_t err;
    esp_netif_t * staNetif;
    esp_netif_t * apNetif;
    wifi_init_config_t initCfg = WIFI_INIT_CONFIG_DEFAULT();
    wifi_config_t staCfg = {0};
    wifi_config_t apCfg = {0};
    size_t apSsidLen;

    err = initNvs();
    if (err == ESP_OK) (void)runtimeConfigLoadFromNvs();

    if (err == ESP_OK)
    {
        esp_err_t credentialErr = credentialsLoad(&enterpriseCredentials);

        if (credentialErr == ESP_OK)
        {
            enterpriseCredentialsReady = 1U;
        }
        else
        {
            enterpriseCredentialsReady = 0U;
            ESP_LOGW(TAG, "WPA2 Enterprise STA will not connect until credentials are provisioned");
        }
    }

    if (err == ESP_OK) err = esp_netif_init();
    if (err == ESP_OK) err = esp_event_loop_create_default();

    if (err == ESP_OK)
    {
        staNetif = esp_netif_create_default_wifi_sta();
        apNetif = esp_netif_create_default_wifi_ap();
        if ((staNetif == NULL) || (apNetif == NULL)) err = ESP_FAIL;
    }

    if (err == ESP_OK) err = esp_wifi_init(&initCfg);
    if (err == ESP_OK) err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, eventHandler, NULL, NULL);
    if (err == ESP_OK) err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, eventHandler, NULL, NULL);

    if (err == ESP_OK)
    {
        if (enterpriseCredentialsReady != 0U)
        {
            copyStringToU8(staCfg.sta.ssid, sizeof(staCfg.sta.ssid), enterpriseCredentials.ssid);
        }

        copyStringToU8(apCfg.ap.ssid, sizeof(apCfg.ap.ssid), SOFTAP_SSID_TXT);
        copyStringToU8(apCfg.ap.password, sizeof(apCfg.ap.password), SOFTAP_PASS_TXT);
        apSsidLen = boundedStringLength(SOFTAP_SSID_TXT, sizeof(apCfg.ap.ssid));

        if (apSsidLen <= 32U)
        {
            apCfg.ap.ssid_len = (uint8_t)apSsidLen;
        }
        else
        {
            err = ESP_ERR_INVALID_ARG;
        }

        apCfg.ap.channel = (uint8_t)SOFTAP_CHANNEL;
        apCfg.ap.max_connection = (uint8_t)SOFTAP_MAX_CONNECTIONS;
        apCfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    }

    if (err == ESP_OK) err = esp_wifi_set_mode(WIFI_MODE_APSTA);

    if ((err == ESP_OK) && (enterpriseCredentialsReady != 0U))
    {
        err = esp_wifi_set_config(WIFI_IF_STA, &staCfg);
    }

    if ((err == ESP_OK) && (enterpriseCredentialsReady != 0U))
    {
        err = initEnterpriseAuth(&enterpriseCredentials);
    }

    if (err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_AP, &apCfg);
    if (err == ESP_OK) err = esp_wifi_start();
    if (err == ESP_OK) err = esp_wifi_set_ps(WIFI_PS_NONE);
    if (err == ESP_OK) err = startHttpServer();

    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "SoftAP SSID: %s", SOFTAP_SSID_TXT);
        ESP_LOGI(TAG, "SoftAP IP: 192.168.4.1");
    }

    return err;
}

static esp_err_t startHttpServer(void)
{
    esp_err_t err = ESP_OK;

    if (httpServer == NULL)
    {
        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.stack_size = 12288U;
        config.lru_purge_enable = true;
        config.recv_wait_timeout = 5;
        config.send_wait_timeout = 5;

        err = httpd_start(&httpServer, &config);
        if (err == ESP_OK) err = httpd_register_uri_handler(httpServer, &rootUri);
        if (err == ESP_OK) err = httpd_register_uri_handler(httpServer, &stateUri);
        if (err == ESP_OK) err = httpd_register_uri_handler(httpServer, &stockfishUri);
        if (err == ESP_OK) err = httpd_register_uri_handler(httpServer, &configGetUri);
        if (err == ESP_OK) err = httpd_register_uri_handler(httpServer, &configPostUri);
        if (err == ESP_OK) err = httpd_register_uri_handler(httpServer, &startUri);
        if (err == ESP_OK) err = httpd_register_uri_handler(httpServer, &drawUri);
        if (err == ESP_OK) err = httpd_register_uri_handler(httpServer, &promoteUri);

        if (err == ESP_OK)
        {
            ESP_LOGI(TAG, "HTTP server started at http://192.168.4.1/");
        }
    }

    return err;
}

static esp_err_t rootHandler(httpd_req_t * req)
{
    static const char html[] =
        "<!DOCTYPE html><html><head><meta charset=\"UTF-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>ESP32-S3 Electronic Chessboard</title>"
        "<style>"
        "body{font-family:Arial,sans-serif;background:#10131a;color:#eee;margin:18px}"
        ".tabs{display:flex;gap:0;margin-bottom:0}.tabs button{border-radius:8px 8px 0 0;border:1px solid #344052;border-bottom:0;background:#1d2430;color:#eee}.tab{display:none}.tab.active{display:block;border:1px solid #344052;border-top:0;background:#141a24;padding:12px;border-radius:0 8px 8px 8px}"
        "button{font-size:17px;margin:4px;padding:9px 13px;border-radius:8px;border:0;cursor:pointer;background:#d7dce8;color:#111}"
        ".layout{display:flex;gap:16px;align-items:flex-start;flex-wrap:wrap}.side{background:#1d2430;padding:10px;border-radius:8px}"
        ".board{display:grid;grid-template-columns:repeat(8,58px);grid-template-rows:repeat(8,58px);border:5px solid #222;background:#222}"
        ".sq{width:58px;height:58px;display:flex;align-items:center;justify-content:center;font-size:32px;box-sizing:border-box;position:relative;font-weight:700}"
        ".light{background:#d9c09b;color:#111}.dark{background:#73543a;color:#111}.coord{position:absolute;right:3px;bottom:2px;font-size:10px;opacity:.75}"
        ".phys{box-shadow:inset 0 0 0 5px #285dff}.turn{box-shadow:inset 0 0 0 7px #4ba3ff}.setupok{background:#064ec9!important;color:#fff}"
        ".legal{background:#d8c52f!important;color:#111}.best{background:#0b9f39!important;color:#fff}.invalid,.check{background:#c01818!important;color:#fff;box-shadow:inset 0 0 0 5px #ffb0b0}"
        ".sel{animation:blink .7s infinite}@keyframes blink{50%{filter:brightness(1.8)}}"
        ".box{background:#1d2430;padding:12px;margin:8px 0;border-radius:8px;max-width:920px}.fen{font-family:monospace;word-break:break-all;font-size:14px}"
        ".score{display:grid;grid-template-columns:1fr 1fr;gap:8px}.score div{background:#141a24;border-radius:6px;padding:8px}"
        ".warn,.drawState{font-size:16px;color:#ffd37a;font-weight:700}.clockState{font-size:20px;color:#ffffff;font-weight:700}.statusCheck{color:#ffb0b0;font-weight:700;font-size:20px}"
        ".grid{display:grid;grid-template-columns:230px 170px;gap:10px;align-items:center}.grid input[type=color]{width:90px;height:36px}.grid input[type=range]{width:190px}"
        ".small{font-size:13px;color:#b8c4d6}.side button{display:block;width:100%;margin:6px 0}"
        "</style></head><body>"
        "<h1>ESP32-S3 Electronic Chessboard</h1>"
        "<div class=\"tabs\"><button onclick=\"showTab('boardTab')\">Board</button><button onclick=\"showTab('configTab')\">Configuration</button></div>"
        "<div id=\"boardTab\" class=\"tab active\">"
        "<div><button onclick=\"startGame()\">Start</button><button onclick=\"promote('Q')\">Queen</button><button onclick=\"promote('R')\">Rook</button><button onclick=\"promote('B')\">Bishop</button><button onclick=\"promote('N')\">Knight</button></div>"
        "<div id=\"drawInfo\" class=\"box drawState\">No active draw offer.</div><div id=\"clock\" class=\"box clockState\">Clock unavailable</div>"
        "<div class=\"layout\">"
        "<div class=\"side\"><h3>White draw</h3><button onclick=\"drawAction('propose','white')\">Propose draw</button><button onclick=\"drawAction('accept','white')\">Accept black offer</button><button onclick=\"drawAction('reject','white')\">Reject black offer</button></div>"
        "<div class=\"board\" id=\"board\"></div>"
        "<div class=\"side\"><h3>Black draw</h3><button onclick=\"drawAction('propose','black')\">Propose draw</button><button onclick=\"drawAction('accept','black')\">Accept white offer</button><button onclick=\"drawAction('reject','black')\">Reject white offer</button></div>"
        "<div><div class=\"box\" id=\"info\"></div><div class=\"box score\" id=\"captures\"></div><div class=\"box\" id=\"setup\"></div><div class=\"box fen\" id=\"fen\"></div><div class=\"box fen\" id=\"pgn\"></div><div class=\"box fen\" id=\"stockfish\"></div></div>"
        "</div></div>"
        "<div id=\"configTab\" class=\"tab\">"
        "<div class=\"box\"><h2>Runtime configuration</h2><div class=\"grid\">"
        "<label>Invalid-position infractions</label><input id=\"cfgInfractions\" type=\"checkbox\"><label>Empty square LEDs</label><input id=\"cfgEmptyEnabled\" type=\"checkbox\"><label>StockfishOnline advisor</label><input id=\"cfgStockfishEnabled\" type=\"checkbox\"><label>Stockfish depth</label><select id=\"cfgStockfishDepth\"><option>10</option><option>11</option><option>12</option><option>13</option><option>14</option><option>15</option></select><label>Clock time (minutes)</label><input id=\"cfgClockMinutes\" type=\"number\" min=\"0\" max=\"360\" value=\"5\"><label>Move bonus (seconds)</label><input id=\"cfgClockBonus\" type=\"number\" min=\"0\" max=\"600\" value=\"0\">"
        "<label>LED brightness</label><input id=\"cfgBrightness\" type=\"range\" min=\"0\" max=\"100\" oninput=\"document.getElementById('brightnessText').textContent=this.value+'%'\">"
        "<span></span><span id=\"brightnessText\">-</span>"
        "<label>Empty square</label><input id=\"cfgEmpty\" type=\"color\">"
        "<label>Piece present</label><input id=\"cfgPiece\" type=\"color\">"
        "<label>Lifted / own turn</label><input id=\"cfgLifted\" type=\"color\">"
        "<label>Legal move</label><input id=\"cfgLegal\" type=\"color\">"
        "<label>Best Stockfish move</label><input id=\"cfgBest\" type=\"color\">"
        "<label>Invalid move</label><input id=\"cfgInvalid\" type=\"color\">"
        "<label>Check</label><input id=\"cfgCheck\" type=\"color\">"
        "<label>Draw</label><input id=\"cfgDraw\" type=\"color\">"
        "</div><br><button onclick=\"saveConfig()\">Save configuration</button><button onclick=\"resetConfig()\">Default configuration</button><button onclick=\"loadConfig()\">Reload</button>"
        "<div id=\"configStatus\" class=\"small\">-</div></div></div>"
        "<script>"
        "const pcs={P:'\\u2659',N:'\\u2658',B:'\\u2657',R:'\\u2656',Q:'\\u2655',K:'\\u2654',p:'\\u265f',n:'\\u265e',b:'\\u265d',r:'\\u265c',q:'\\u265b',k:'\\u265a'};"
        "function el(id){return document.getElementById(id);}function showTab(id){document.querySelectorAll('.tab').forEach(t=>t.classList.remove('active'));el(id).classList.add('active');if(id==='configTab')loadConfig();}"
        "function bit(arr,sq){let f=sq.charCodeAt(0)-97,r=sq.charCodeAt(1)-49;return Array.isArray(arr)&&((arr[r]>>f)&1)!==0;}"
        "async function startGame(){await fetch('/api/start',{method:'POST'});update();}async function promote(p){await fetch('/api/promote?p='+p,{method:'POST'});update();}"
        "async function drawAction(action,side){let r=await fetch('/api/draw?action='+action+'&side='+side,{method:'POST'});el('drawInfo').textContent=await r.text();update();}"
        "function virtualSq(s,sq){if(s.orientation==='flipped'){let r=9-parseInt(sq[1]);return sq[0]+r;}return sq;}"
        "function cellPiece(fen,sq){let rows=fen.split(' ')[0].split('/');let r=8-parseInt(sq[1]),f=sq.charCodeAt(0)-97,x=0;if(!rows[r])return '';for(const ch of rows[r]){if(ch>='1'&&ch<='8')x+=parseInt(ch);else{if(x===f)return pcs[ch]||'';x++;}}return '';}"
        "function setupLists(s){let missing=[],extra=[];for(let r=1;r<=8;r++){for(let f=0;f<8;f++){let sq=String.fromCharCode(97+f)+r;if(bit(s.invalid,sq)){if(bit(s.physical,sq))extra.push(sq);else missing.push(sq);}}}return {missing,extra};}"
        "function hasCheck(s){for(let r=1;r<=8;r++){for(let f=0;f<8;f++){if(bit(s.check,String.fromCharCode(97+f)+r))return true;}}return false;}function fmtClock(ms){let t=Math.ceil((ms||0)/1000);let m=Math.floor(t/60);let sec=t%60;return m+':'+String(sec).padStart(2,'0');}"
        "async function loadConfig(){try{let c=await (await fetch('/api/config')).json();el('cfgInfractions').checked=!!c.infractions;el('cfgEmptyEnabled').checked=!!c.empty_enabled;el('cfgStockfishEnabled').checked=!!c.stockfish_enabled;el('cfgStockfishDepth').value=c.stockfish_depth;el('cfgClockMinutes').value=Math.floor((c.clock_initial_seconds||0)/60);el('cfgClockBonus').value=c.clock_bonus_seconds||0;el('cfgBrightness').value=c.brightness;el('brightnessText').textContent=c.brightness+'%';el('cfgEmpty').value=c.empty;el('cfgPiece').value=c.piece;el('cfgLifted').value=c.lifted;el('cfgLegal').value=c.legal;el('cfgBest').value=c.best;el('cfgInvalid').value=c.invalid;el('cfgCheck').value=c.check;el('cfgDraw').value=c.draw;el('configStatus').textContent='Loaded';}catch(e){el('configStatus').textContent='Config API unavailable';}}"
        "async function resetConfig(){let r=await fetch('/api/config?reset=1',{method:'POST'});el('configStatus').textContent=await r.text();await loadConfig();update();}async function saveConfig(){let p=new URLSearchParams();p.set('infractions',el('cfgInfractions').checked?'1':'0');p.set('empty_enabled',el('cfgEmptyEnabled').checked?'1':'0');p.set('stockfish_enabled',el('cfgStockfishEnabled').checked?'1':'0');p.set('stockfish_depth',el('cfgStockfishDepth').value);p.set('clock_initial_seconds',String((parseInt(el('cfgClockMinutes').value,10)||0)*60));p.set('clock_bonus_seconds',el('cfgClockBonus').value);p.set('brightness',el('cfgBrightness').value);p.set('empty',el('cfgEmpty').value.substring(1));p.set('piece',el('cfgPiece').value.substring(1));p.set('lifted',el('cfgLifted').value.substring(1));p.set('legal',el('cfgLegal').value.substring(1));p.set('best',el('cfgBest').value.substring(1));p.set('invalid',el('cfgInvalid').value.substring(1));p.set('check',el('cfgCheck').value.substring(1));p.set('draw',el('cfgDraw').value.substring(1));let r=await fetch('/api/config?'+p.toString(),{method:'POST'});el('configStatus').textContent=await r.text();update();}"
        "async function update(){let s=await (await fetch('/api/state')).json();let b=el('board');let isSetup=s.mode==='SETUP';b.innerHTML='';for(let r=8;r>=1;r--){for(let f=0;f<8;f++){let sq=String.fromCharCode(97+f)+r;let phys=bit(s.physical,sq),inv=bit(s.invalid,sq);let c='sq '+(((r+f)%2)?'dark':'light');if(isSetup&&phys&&!inv)c+=' setupok';else if(phys)c+=' phys';if(bit(s.turn,sq))c+=' turn';if(bit(s.legal,sq))c+=' legal';if(bit(s.best,sq))c+=' best';if(inv)c+=' invalid';if(bit(s.check,sq))c+=' check';if(s.selected===sq)c+=' sel';let d=document.createElement('div');d.className=c;d.textContent=isSetup?(phys?'\\u25cf':(inv?sq:'')):cellPiece(s.fen,virtualSq(s,sq));let cc=document.createElement('span');cc.className='coord';cc.textContent=sq;d.appendChild(cc);b.appendChild(d);}}let lists=setupLists(s);let warn=(isSetup&&lists.missing.length+lists.extra.length>0)?'<div class=\"warn\">Fix red squares, then press Start again.</div>':'';let checkText=(s.state==='CHECK'||s.state==='CHECKMATE')?'<div class=\"statusCheck\">'+s.state+'</div>':'';el('drawInfo').textContent=s.draw_offer?('Draw offered by '+s.draw_offer_by+'. Opponent may accept or reject.'):((s.state==='DRAW'||s.state==='STALEMATE')?('Game result: '+s.state):'No active draw offer.');el('clock').innerHTML='<b>Clock</b><br>White: '+fmtClock(s.white_clock_ms)+'<br>Black: '+fmtClock(s.black_clock_ms)+'<br>Bonus: '+(s.clock_bonus_seconds||0)+'s/move';el('info').innerHTML=checkText+'<b>Mode:</b> '+s.mode+'<br><b>State:</b> '+s.state+'<br><b>Turn:</b> '+s.turn_text+'<br><b>Orientation:</b> '+s.orientation+'<br><b>Selected:</b> '+s.selected+'<br><b>Legal:</b> '+s.legal_text+'<br><b>Best:</b> '+s.best_move;el('captures').innerHTML='<div><b>White side captured</b><br>'+s.white_captured+'<br><b>Points:</b> '+s.white_score+'<br><b>Infractions:</b> '+(s.white_infractions||0)+'/2</div><div><b>Black side captured</b><br>'+s.black_captured+'<br><b>Points:</b> '+s.black_score+'<br><b>Infractions:</b> '+(s.black_infractions||0)+'/2</div>';el('setup').innerHTML=warn+'<b>Missing:</b> '+(lists.missing.join(' ')||'-')+'<br><b>Extra:</b> '+(lists.extra.join(' ')||'-');el('fen').textContent=s.fen;el('pgn').textContent=s.pgn;fetch('/api/stockfish').then(r=>r.text()).then(t=>{el('stockfish').textContent='Stockfish JSON: '+(t||'-');}).catch(()=>{el('stockfish').textContent='Stockfish JSON: unavailable';});}"
        "setInterval(update,500);update();loadConfig();"
        "</script></body></html>";

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    httpd_resp_set_hdr(req, "Expires", "0");
    return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}



static void jsonArray8(char * dst, size_t len, const uint8_t map[8])
{
    (void)snprintf(dst, len, "[%u,%u,%u,%u,%u,%u,%u,%u]", map[0], map[1], map[2], map[3], map[4], map[5], map[6], map[7]);
}


/* CONFIG_HANDLER_HELPERS */
static uint32_t parseQueryUint(const char * text, uint32_t fallback, uint32_t maxValue)
{
    uint32_t value = 0U;
    bool any = false;

    if (text == NULL)
    {
        return fallback;
    }

    for (size_t index = 0U; text[index] != '\0'; index++)
    {
        if ((text[index] >= '0') && (text[index] <= '9'))
        {
            value = (value * 10U) + (uint32_t)(text[index] - '0');
            any = true;
        }
        else
        {
            break;
        }
    }

    if (any == false)
    {
        value = fallback;
    }

    if (value > maxValue)
    {
        value = maxValue;
    }

    return value;
}

static uint8_t queryBoolValue(const char * query, const char * key, uint8_t fallback)
{
    char value[8];

    if ((query != NULL) &&
        (key != NULL) &&
        (httpd_query_key_value(query, key, value, sizeof(value)) == ESP_OK))
    {
        if ((value[0] == '0') || (value[0] == 'f') || (value[0] == 'F') ||
            (value[0] == 'n') || (value[0] == 'N'))
        {
            return 0U;
        }

        return 1U;
    }

    return fallback;
}

static uint8_t queryUint8Value(const char * query, const char * key, uint8_t fallback, uint8_t maxValue)
{
    char value[16];

    if ((query != NULL) &&
        (key != NULL) &&
        (httpd_query_key_value(query, key, value, sizeof(value)) == ESP_OK))
    {
        return (uint8_t)parseQueryUint(value, fallback, maxValue);
    }

    return fallback;
}


static uint16_t queryUint16Value(const char * query, const char * key, uint16_t fallback, uint16_t maxValue)
{
    char value[16];

    if ((query != NULL) &&
        (key != NULL) &&
        (httpd_query_key_value(query, key, value, sizeof(value)) == ESP_OK))
    {
        return (uint16_t)parseQueryUint(value, fallback, maxValue);
    }

    return fallback;
}


static bool hexNibble(char ch, uint8_t * value)
{
    bool ok = true;

    if (value == NULL)
    {
        return false;
    }

    if ((ch >= '0') && (ch <= '9'))
    {
        *value = (uint8_t)(ch - '0');
    }
    else if ((ch >= 'a') && (ch <= 'f'))
    {
        *value = (uint8_t)(10U + (uint8_t)(ch - 'a'));
    }
    else if ((ch >= 'A') && (ch <= 'F'))
    {
        *value = (uint8_t)(10U + (uint8_t)(ch - 'A'));
    }
    else
    {
        ok = false;
    }

    return ok;
}

static bool parseHexColor(const char * text, project_rgb_t * color)
{
    uint8_t n[6];
    size_t offset = 0U;

    if ((text == NULL) || (color == NULL))
    {
        return false;
    }

    if (text[0] == '#')
    {
        offset = 1U;
    }

    for (uint8_t index = 0U; index < 6U; index++)
    {
        if (hexNibble(text[offset + index], &n[index]) == false)
        {
            return false;
        }
    }

    color->r = (uint8_t)((n[0] << 4U) | n[1]);
    color->g = (uint8_t)((n[2] << 4U) | n[3]);
    color->b = (uint8_t)((n[4] << 4U) | n[5]);

    return true;
}

static void applyColorQuery(const char * query, const char * key, runtime_led_color_id_t colorId)
{
    char value[16];
    project_rgb_t color;

    if ((query != NULL) &&
        (key != NULL) &&
        (httpd_query_key_value(query, key, value, sizeof(value)) == ESP_OK) &&
        (parseHexColor(value, &color) != false))
    {
        (void)runtimeConfigSetLedColor(colorId, color);
    }
}

static void appendColorJson(char * dst, size_t dstLen, size_t * pos, const char * key, project_rgb_t color)
{
    if ((dst == NULL) || (pos == NULL) || (key == NULL) || (*pos >= dstLen))
    {
        return;
    }

    *pos += (size_t)snprintf(
        &dst[*pos],
        dstLen - *pos,
        ",\"%s\":\"#%02X%02X%02X\"",
        key,
        (unsigned int)color.r,
        (unsigned int)color.g,
        (unsigned int)color.b
    );
}

static esp_err_t apiConfigHandler(httpd_req_t * req)
{
    char query[384];
    char response[768];
    size_t pos = 0U;
    const runtime_config_t * cfg;

    if (req == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    query[0] = '\0';

    if ((req->method == HTTP_POST) &&
        (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK))
    {
        const runtime_config_t * current = runtimeConfigGet();
        uint8_t stockfishWasEnabled = current->stockfish_enabled;

        if (queryBoolValue(query, "reset", 0U) != 0U)
        {
            (void)runtimeConfigReset();
        }
        else
        {
            (void)runtimeConfigSetInvalidPositionInfractions(
                queryBoolValue(query, "infractions", current->invalid_position_infractions_enabled)
            );

            (void)runtimeConfigSetLedEmptyEnabled(
                queryBoolValue(query, "empty_enabled", current->led_empty_enabled)
            );

            (void)runtimeConfigSetStockfishEnabled(
                queryBoolValue(query, "stockfish_enabled", current->stockfish_enabled)
            );

            (void)runtimeConfigSetStockfishDepth(
                queryUint8Value(query, "stockfish_depth", current->stockfish_depth, 15U)
            );

            (void)runtimeConfigSetClockInitialSeconds(
                queryUint16Value(query, "clock_initial_seconds", current->clock_initial_seconds, CLOCK_CONFIG_MAX_SECONDS)
            );

            (void)runtimeConfigSetClockBonusSeconds(
                queryUint16Value(query, "clock_bonus_seconds", current->clock_bonus_seconds, CLOCK_BONUS_MAX_SECONDS)
            );

            (void)runtimeConfigSetLedBrightness(
                queryUint8Value(query, "brightness", current->led_brightness_percent, 100U)
            );

            applyColorQuery(query, "empty", RUNTIME_LED_COLOR_EMPTY);
            applyColorQuery(query, "piece", RUNTIME_LED_COLOR_PIECE);
            applyColorQuery(query, "lifted", RUNTIME_LED_COLOR_LIFTED);
            applyColorQuery(query, "legal", RUNTIME_LED_COLOR_LEGAL);
            applyColorQuery(query, "best", RUNTIME_LED_COLOR_BEST);
            applyColorQuery(query, "invalid", RUNTIME_LED_COLOR_INVALID);
            applyColorQuery(query, "check", RUNTIME_LED_COLOR_CHECK);
            applyColorQuery(query, "draw", RUNTIME_LED_COLOR_DRAW);
        }

        refreshStockfishAdvisorAfterConfig(stockfishWasEnabled);
    }

    cfg = runtimeConfigGet();

    pos += (size_t)snprintf(
        response,
        sizeof(response),
        "{\"infractions\":%u,\"empty_enabled\":%u,\"stockfish_enabled\":%u,\"stockfish_depth\":%u,\"clock_initial_seconds\":%u,\"clock_bonus_seconds\":%u,\"brightness\":%u",
        (unsigned int)cfg->invalid_position_infractions_enabled,
        (unsigned int)cfg->led_empty_enabled,
        (unsigned int)cfg->stockfish_enabled,
        (unsigned int)cfg->stockfish_depth,
        (unsigned int)cfg->clock_initial_seconds,
        (unsigned int)cfg->clock_bonus_seconds,
        (unsigned int)cfg->led_brightness_percent
    );

    appendColorJson(response, sizeof(response), &pos, "empty", cfg->led_empty_rgb);
    appendColorJson(response, sizeof(response), &pos, "piece", cfg->led_piece_rgb);
    appendColorJson(response, sizeof(response), &pos, "lifted", cfg->led_lifted_rgb);
    appendColorJson(response, sizeof(response), &pos, "legal", cfg->led_legal_rgb);
    appendColorJson(response, sizeof(response), &pos, "best", cfg->led_best_rgb);
    appendColorJson(response, sizeof(response), &pos, "invalid", cfg->led_invalid_rgb);
    appendColorJson(response, sizeof(response), &pos, "check", cfg->led_check_rgb);
    appendColorJson(response, sizeof(response), &pos, "draw", cfg->led_draw_rgb);

    if (pos < (sizeof(response) - 2U))
    {
        response[pos] = '}';
        response[pos + 1U] = '\0';
    }
    else
    {
        response[sizeof(response) - 2U] = '}';
        response[sizeof(response) - 1U] = '\0';
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    return httpd_resp_sendstr(req, response);
}


static esp_err_t apiStockfishHandler(httpd_req_t * req)
{
    char stockfishJson[STOCKFISH_CLIENT_JSON_TEXT_LEN];

    stockfishJson[0] = '\0';

    if (stateMutex != NULL)
    {
        if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(200U)) == pdPASS)
        {
            (void)snprintf(stockfishJson, sizeof(stockfishJson), "%s", stateStockfishJson);
            (void)xSemaphoreGive(stateMutex);
        }
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");

    if (stockfishJson[0] == '\0')
    {
        return httpd_resp_sendstr(req, "{\"success\":false,\"reason\":\"unavailable\"}");
    }

    return httpd_resp_sendstr(req, stockfishJson);
}


static esp_err_t apiStateHandler(httpd_req_t * req)
{
    char chunk[4096U];
    char physical[64];
    char turn[64];
    char legal[64];
    char best[64];
    char invalid[64];
    char check[64];
    char setupExpected[64];
        char capturedByWhite[96];
    char capturedByBlack[96];
    uint16_t whiteScore = 0U;
    uint16_t blackScore = 0U;
    uint8_t turnMap[8] = {0U};
    uint8_t expectedMap[8] = {0xFFU, 0xFFU, 0x00U, 0x00U, 0x00U, 0x00U, 0xFFU, 0xFFU};
    const char * orientationText = "unassigned";
    const char * turnText = "white";
    const char * drawOfferText = "none";
    BaseType_t mutexTaken = pdFALSE;

    if (stateMutex != NULL)
    {
        mutexTaken = xSemaphoreTake(stateMutex, pdMS_TO_TICKS(1000U));
        if (mutexTaken != pdPASS)
        {
            httpd_resp_set_status(req, "503 Service Unavailable");
            httpd_resp_set_type(req, "application/json");
            return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"state_lock_timeout\"}");
        }
    }

    /* CLOCK_UPDATE_API_STATE */
    clockUpdate();

    buildCapturedMaterialText(CHESS_WHITE, capturedByWhite, sizeof(capturedByWhite), &whiteScore);
    buildCapturedMaterialText(CHESS_BLACK, capturedByBlack, sizeof(capturedByBlack), &blackScore);

    if (orientationKnown != 0U)
    {
        orientationText = (orientationFlipRanks != 0U) ? "flipped" : "normal";
        turnText = (game.side_to_move == CHESS_WHITE) ? "white" : "black";

        for (uint8_t row = 0U; row < 8U; row++)
        {
            for (uint8_t col = 0U; col < 8U; col++)
            {
                chess_piece_t p = game.board[row][col];
                if ((p.type != CHESS_EMPTY) && (p.color == game.side_to_move))
                {
                    setVirtualBit(turnMap, row, col);
                }
            }
        }
    }
    else if (gameMode == GAME_MODE_RUNNING)
    {
        turnText = "white: first moved side";
    }

    if (drawOfferActive != 0U)
    {
        drawOfferText = (drawOfferByWhite != 0U) ? "white" : "black";
    }

    jsonArray8(physical, sizeof(physical), physicalPresence);
    jsonArray8(turn, sizeof(turn), turnMap);
    jsonArray8(legal, sizeof(legal), legalMap);
    jsonArray8(best, sizeof(best), bestMap);
    jsonArray8(invalid, sizeof(invalid), invalidMap);
    jsonArray8(check, sizeof(check), checkMap);
    jsonArray8(setupExpected, sizeof(setupExpected), expectedMap);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    (void)snprintf(
        chunk,
        sizeof(chunk),
        "{\"mode\":\"%s\",\"state\":\"%s\",\"turn_text\":\"%s\",\"orientation\":\"%s\",\"fen\":\"%s\",\"pgn\":\"%s\",\"selected\":\"%s\",\"legal_text\":\"%s\",\"best_move\":\"%s\",\"white_captured\":\"%s\",\"black_captured\":\"%s\",\"white_score\":%u,\"black_score\":%u,\"white_infractions\":%u,\"black_infractions\":%u,\"draw_offer\":%u,\"draw_offer_by\":\"%s\",\"white_clock_ms\":%lu,\"black_clock_ms\":%lu,\"clock_active\":%u,\"clock_initial_seconds\":%u,\"clock_bonus_seconds\":%u,\"physical\":%s,\"turn\":%s,\"legal\":%s,\"best\":%s,\"invalid\":%s,\"check\":%s,\"setup_expected\":%s}",
        modeToText(gameMode),
        stateText,
        turnText,
        orientationText,
        game.fen,
        game.pgn,
        (selectedValid != 0U) ? selectedSquare : "-",
        stateLegal,
        stateBest,
        capturedByWhite,
        capturedByBlack,
        (unsigned int)whiteScore,
        (unsigned int)blackScore,
        (unsigned int)whiteInfractions,
        (unsigned int)blackInfractions,
        (unsigned int)drawOfferActive,
        drawOfferText,
        (unsigned long)whiteClockMs,
        (unsigned long)blackClockMs,
        (unsigned int)clockActive,
        (unsigned int)runtimeConfigGet()->clock_initial_seconds,
        (unsigned int)runtimeConfigGet()->clock_bonus_seconds,
        physical,
        turn,
        legal,
        best,
        invalid,
        check,
        setupExpected
    );

    if (mutexTaken == pdPASS)
    {
        (void)xSemaphoreGive(stateMutex);
    }

    return httpd_resp_sendstr(req, chunk);
}



static esp_err_t apiStartHandler(httpd_req_t * req)
{
    game_command_t command;

    command.type = GAME_COMMAND_START;
    command.promotion = CHESS_EMPTY;

    if (gameCommandQueue != NULL)
    {
        (void)xQueueSend(gameCommandQueue, &command, pdMS_TO_TICKS(200U));
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

static esp_err_t apiPromoteHandler(httpd_req_t * req)
{
    char query[32];
    char pieceText[4];
    game_command_t command = { GAME_COMMAND_PROMOTE, CHESS_QUEEN };

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK)
    {
        if (httpd_query_key_value(query, "p", pieceText, sizeof(pieceText)) == ESP_OK)
        {
            if (pieceText[0] == 'R') command.promotion = CHESS_ROOK;
            else if (pieceText[0] == 'B') command.promotion = CHESS_BISHOP;
            else if (pieceText[0] == 'N') command.promotion = CHESS_KNIGHT;
            else command.promotion = CHESS_QUEEN;
        }
    }

    if (gameCommandQueue != NULL)
    {
        (void)xQueueSend(gameCommandQueue, &command, 0);
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

static esp_err_t apiDrawHandler(httpd_req_t * req)
{
    char query[96];
    char action[16];
    char sideText[8];
    chess_color_t requestSide = CHESS_WHITE;
    uint8_t sideValid = 0U;
    const char * response = "{\"ok\":false,\"error\":\"bad_request\"}";
    BaseType_t mutexTaken = pdFALSE;

    if (req == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    query[0] = '\0';
    action[0] = '\0';
    sideText[0] = '\0';

    if (req->method != HTTP_POST)
    {
        httpd_resp_set_status(req, "405 Method Not Allowed");
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"method_not_allowed\"}");
    }

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK)
    {
        (void)httpd_query_key_value(query, "action", action, sizeof(action));
        (void)httpd_query_key_value(query, "side", sideText, sizeof(sideText));
    }

    if (strcmp(sideText, "white") == 0)
    {
        requestSide = CHESS_WHITE;
        sideValid = 1U;
    }
    else if (strcmp(sideText, "black") == 0)
    {
        requestSide = CHESS_BLACK;
        sideValid = 1U;
    }

    if ((action[0] == '\0') || (sideValid == 0U))
    {
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, response);
    }

    if (stateMutex != NULL)
    {
        mutexTaken = xSemaphoreTake(stateMutex, pdMS_TO_TICKS(1000U));
    }

    if (mutexTaken != pdPASS)
    {
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"state_lock_timeout\"}");
    }

    if (gameMode != GAME_MODE_RUNNING)
    {
        response = "{\"ok\":false,\"error\":\"game_not_running\"}";
    }
    else if (strcmp(action, "propose") == 0)
    {
        drawOfferActive = 1U;
        drawOfferByWhite = (requestSide == CHESS_WHITE) ? 1U : 0U;
        setMode(GAME_MODE_RUNNING, "DRAW_OFFERED");
        sendLedFrame(0U);
        response = "{\"ok\":true,\"draw_offer\":true}";
    }
    else if (strcmp(action, "accept") == 0)
    {
        const uint8_t requestByWhite = (requestSide == CHESS_WHITE) ? 1U : 0U;

        if ((drawOfferActive != 0U) && (requestByWhite != drawOfferByWhite))
        {
            drawOfferActive = 0U;
            declareDraw();
            response = "{\"ok\":true,\"draw\":true}";
        }
        else
        {
            response = "{\"ok\":false,\"error\":\"no_opponent_draw_offer\"}";
        }
    }
    else if (strcmp(action, "reject") == 0)
    {
        const uint8_t requestByWhite = (requestSide == CHESS_WHITE) ? 1U : 0U;

        if ((drawOfferActive != 0U) && (requestByWhite != drawOfferByWhite))
        {
            drawOfferActive = 0U;
            setMode(GAME_MODE_RUNNING, "DRAW_REJECTED");
            sendLedFrame(0U);
            response = "{\"ok\":true,\"draw_rejected\":true}";
        }
        else
        {
            response = "{\"ok\":false,\"error\":\"no_opponent_draw_offer\"}";
        }
    }
    else
    {
        response = "{\"ok\":false,\"error\":\"unknown_draw_action\"}";
    }

    (void)xSemaphoreGive(stateMutex);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    return httpd_resp_sendstr(req, response);
}


void serverTask(void * parameters)
{
    sensor_event_t event;
    BaseType_t status;

    (void)parameters;
    ESP_LOGI(TAG, "Chess controller started in setup mode");
    sendLedFrame(0U);

    for (;;)
    {
        if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(1000U)) == pdPASS)
        {
            processPendingCommands();
            /* CLOCK_UPDATE_SERVER_LOOP */
            clockUpdate();
            (void)xSemaphoreGive(stateMutex);
        }

        status = xQueueReceive(sensorQueueRef, &event, pdMS_TO_TICKS(40U));

        if (status == pdPASS)
        {
            if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(1000U)) == pdPASS)
            {
                handleSensorEvent(&event);
                processStockfishResponses();
                (void)xSemaphoreGive(stateMutex);
            }
        }
    }
}
