#include "stockfish_client.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_err.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "project_config.h"

#define STOCKFISH_URL_LEN (512U)

static const char * const TAG = "STOCKFISH";

typedef struct
{
    char * text;
    size_t len;
    size_t capacity;
} stockfish_http_capture_t;

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

static bool isMoveText(const char * text)
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

static bool extractBestMoveFromJson(const char * input, char bestMove[APP_MOVE_TEXT_LEN])
{
    const char * bestField;
    const char * bestToken;
    bool found = false;

    if ((input == NULL) || (bestMove == NULL))
    {
        return false;
    }

    bestMove[0] = '\0';
    bestField = strstr(input, "\"bestmove\"");

    if (bestField == NULL)
    {
        return false;
    }

    bestToken = strstr(bestField, "bestmove ");

    if (bestToken == NULL)
    {
        return false;
    }

    bestToken += 9;

    if (isMoveText(bestToken) != false)
    {
        bestMove[0] = bestToken[0];
        bestMove[1] = bestToken[1];
        bestMove[2] = bestToken[2];
        bestMove[3] = bestToken[3];

        if ((bestToken[4] == 'q') || (bestToken[4] == 'r') ||
            (bestToken[4] == 'b') || (bestToken[4] == 'n') ||
            (bestToken[4] == 'Q') || (bestToken[4] == 'R') ||
            (bestToken[4] == 'B') || (bestToken[4] == 'N'))
        {
            bestMove[4] = bestToken[4];
            bestMove[5] = '\0';
        }
        else
        {
            bestMove[4] = '\0';
        }

        found = true;
    }

    return found;
}

static bool appendUrlEncoded(char * dst, size_t dstLen, size_t * pos, const char * src)
{
    bool ok = true;

    if ((dst == NULL) || (pos == NULL) || (src == NULL) || (*pos >= dstLen))
    {
        return false;
    }

    for (size_t index = 0U; src[index] != '\0'; index++)
    {
        unsigned char ch = (unsigned char)src[index];

        if (((ch >= (unsigned char)'a') && (ch <= (unsigned char)'z')) ||
            ((ch >= (unsigned char)'A') && (ch <= (unsigned char)'Z')) ||
            ((ch >= (unsigned char)'0') && (ch <= (unsigned char)'9')) ||
            (ch == (unsigned char)'-') ||
            (ch == (unsigned char)'_') ||
            (ch == (unsigned char)'.') ||
            (ch == (unsigned char)'~'))
        {
            if ((*pos + 1U) >= dstLen)
            {
                ok = false;
                break;
            }

            dst[*pos] = (char)ch;
            *pos += 1U;
            dst[*pos] = '\0';
        }
        else
        {
            if ((*pos + 3U) >= dstLen)
            {
                ok = false;
                break;
            }

            (void)snprintf(&dst[*pos], dstLen - *pos, "%%%02X", (unsigned int)ch);
            *pos += 3U;
        }
    }

    return ok;
}

static esp_err_t httpEventHandler(esp_http_client_event_t * event)
{
    if ((event != NULL) &&
        (event->event_id == HTTP_EVENT_ON_DATA) &&
        (event->user_data != NULL) &&
        (event->data != NULL) &&
        (event->data_len > 0))
    {
        stockfish_http_capture_t * capture = (stockfish_http_capture_t *)event->user_data;
        size_t available;
        size_t toCopy = (size_t)event->data_len;

        if ((capture->text == NULL) || (capture->capacity == 0U) || (capture->len >= capture->capacity))
        {
            return ESP_OK;
        }

        available = capture->capacity - capture->len - 1U;

        if (toCopy > available)
        {
            toCopy = available;
        }

        if (toCopy > 0U)
        {
            (void)memcpy(&capture->text[capture->len], event->data, toCopy);
            capture->len += toCopy;
            capture->text[capture->len] = '\0';
        }
    }

    return ESP_OK;
}

bool stockfishClientAnalyzeFen(const char * fen, stockfish_client_result_t * result)
{
    const project_config_t * cfg;
    char url[STOCKFISH_URL_LEN];
    size_t pos;
    esp_http_client_handle_t client;
    esp_http_client_config_t config = {0};
    stockfish_http_capture_t capture;
    esp_err_t err;
    bool ok = false;
    size_t responseCapacity;

    if ((fen == NULL) || (result == NULL))
    {
        return false;
    }

    cfg = projectConfigGet();
    (void)memset(result, 0, sizeof(*result));

    if ((cfg->stockfish_enabled == 0U) || (cfg->stockfish_url[0] == '\0'))
    {
        ESP_LOGW(TAG, "StockfishOnline advisor disabled");
        return false;
    }

    responseCapacity = (size_t)cfg->stockfish_response_bytes;

    if (responseCapacity > sizeof(result->json))
    {
        responseCapacity = sizeof(result->json);
    }

    if (responseCapacity < 64U)
    {
        responseCapacity = 64U;
    }

    capture.text = result->json;
    capture.len = 0U;
    capture.capacity = responseCapacity;
    result->json[0] = '\0';

    (void)snprintf(
        url,
        sizeof(url),
        "%s?depth=%u&fen=",
        cfg->stockfish_url,
        (unsigned int)cfg->stockfish_depth
    );

    pos = boundedStringLength(url, sizeof(url));

    if (appendUrlEncoded(url, sizeof(url), &pos, fen) == false)
    {
        ESP_LOGW(TAG, "StockfishOnline URL too long");
        return false;
    }

    config.url = url;
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = (int)cfg->stockfish_timeout_ms;
    config.event_handler = httpEventHandler;
    config.user_data = &capture;
    config.crt_bundle_attach = esp_crt_bundle_attach;

    client = esp_http_client_init(&config);

    if (client == NULL)
    {
        return false;
    }

    err = esp_http_client_perform(client);

    if (err == ESP_OK)
    {
        int status = esp_http_client_get_status_code(client);

        if ((status >= 200) && (status < 300))
        {
            ok = extractBestMoveFromJson(result->json, result->bestMove);

            if (ok != false)
            {
                result->valid = 1U;
            }
        }
        else
        {
            ESP_LOGW(TAG, "StockfishOnline HTTP status: %d", status);
        }
    }
    else
    {
        ESP_LOGW(TAG, "StockfishOnline request failed: %ld", (long)err);
    }

    esp_http_client_cleanup(client);
    return ok;
}

bool stockfishClientBestMove(const char * fen, char bestMove[APP_MOVE_TEXT_LEN])
{
    stockfish_client_result_t result;
    bool ok;

    if (bestMove == NULL)
    {
        return false;
    }

    bestMove[0] = '\0';

    ok = stockfishClientAnalyzeFen(fen, &result);

    if (ok != false)
    {
        (void)snprintf(bestMove, APP_MOVE_TEXT_LEN, "%s", result.bestMove);
    }

    return ok;
}
