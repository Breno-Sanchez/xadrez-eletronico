#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "app_types.h"
#include "esp_crt_bundle.h"
#include "esp_eap_client.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "server.h"

#define WIFI_SSID_TXT              "CONFIGURE_WIFI_SSID"
#define EAP_IDENTITY_TXT           "CONFIGURE_EAP_IDENTITY"
#define EAP_USERNAME_TXT           "CONFIGURE_EAP_USERNAME"
#define WIFI_PASS_TXT              "CONFIGURE_WIFI_PASSWORD"
#define SOFTAP_SSID_TXT           "XADREZ_ESP"
#define SOFTAP_PASS_TXT           "configure123"
#define SOFTAP_CHANNEL            (1U)
#define SOFTAP_MAX_CONNECTIONS    (2U)

#define EAP_PASSWORD_TXT           "CONFIGURE_EAP_PASSWORD"

#define EAP_MAX_LEN                (127U)
#define WIFI_WAIT_MS               (1000U)

#define LOCAL_HTML_BUF_LEN         (8192U)
#define STATE_TEXT_LEN             (32U)
#define FEN_TEXT_LEN               (128U)
#define LEGAL_TEXT_LEN             (64U)

#define HTTPS_URL_LEN              (256U)
#define HTTPS_RESPONSE_LEN         (1024U)

#define LICHESS_CLOUD_EVAL_URL     "https://lichess.org/api/cloud-eval?fen="
#define LICHESS_URL_SUFFIX         "&multiPv=1"

#define FEN_WITH_KNIGHT_ENCODED    "rnbqkbnr%2Fpppppppp%2F8%2F8%2F8%2F8%2FPPPPPPPP%2FRNBQKBNR%20w%20KQkq%20-%200%201"
#define FEN_WITH_KNIGHT_TEXT       "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"
#define FEN_NO_KNIGHT_TEXT         "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKB1R w KQkq - 0 1"

static const char * const TAG = "SERVER";

static QueueHandle_t sensorQueueRef = NULL;
static QueueHandle_t ledQueueRef = NULL;
static SemaphoreHandle_t stateMutex = NULL;
static httpd_handle_t httpServer = NULL;

static uint8_t wifiConnected = 0U;

static char stateFen[FEN_TEXT_LEN] = FEN_WITH_KNIGHT_TEXT;
static char statePiece[STATE_TEXT_LEN] = "PRESENT";
static char stateBest[APP_MOVE_TEXT_LEN] = "-----";
static char stateLegal[LEGAL_TEXT_LEN] = "-";

static size_t boundedStringLength(const char * text, size_t maxLen);
static void copyText(char * dst, size_t dstLen, const char * src);
static void copyStringToU8(uint8_t * dst, size_t dstLen, const char * src);
static void setSquare(char dst[APP_SQUARE_TEXT_LEN], char file, char rank);
static void setMoveText(char dst[APP_MOVE_TEXT_LEN], const char * src);

static esp_err_t initNvs(void);
static esp_err_t setEapIdentity(void);
static esp_err_t setEapUsername(void);
static esp_err_t setEapPassword(void);
static esp_err_t initEnterpriseAuth(void);

static void eventHandler(void * arg, esp_event_base_t base, int32_t id, void * data);

static esp_err_t startHttpServer(void);
static esp_err_t rootHandler(httpd_req_t * req);

static esp_err_t queryLichessBestMove(char bestMove[APP_MOVE_TEXT_LEN]);
static esp_err_t buildLichessUrl(char * url, size_t urlLen);
static esp_err_t httpsGet(const char * url, char * response, size_t responseLen);
static uint8_t parseBestMoveFromJson(const char * json, char bestMove[APP_MOVE_TEXT_LEN]);

static void updateState(const char * fen,
                        const char * piece,
                        const char * best,
                        const char * legal);

static void buildKnightLiftedLedCommand(uint32_t sequence,
                                        const char bestMove[APP_MOVE_TEXT_LEN],
                                        led_command_t * command);
static void buildClearLedCommand(uint32_t sequence, led_command_t * command);

static const httpd_uri_t rootUri = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = rootHandler,
    .user_ctx = NULL
};

esp_err_t serverInit(QueueHandle_t sensorQueue, QueueHandle_t ledQueue)
{
    esp_err_t err = ESP_ERR_INVALID_ARG;

    if ((sensorQueue != NULL) && (ledQueue != NULL))
    {
        sensorQueueRef = sensorQueue;
        ledQueueRef = ledQueue;
        stateMutex = xSemaphoreCreateMutex();

        if (stateMutex != NULL)
        {
            err = ESP_OK;
        }
    }

    return err;
}

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

static void copyText(char * dst, size_t dstLen, const char * src)
{
    size_t index = 0U;

    if ((dst != NULL) && (src != NULL) && (dstLen > 0U))
    {
        while ((src[index] != '\0') && (index < (dstLen - 1U)))
        {
            dst[index] = src[index];
            index++;
        }

        dst[index] = '\0';
    }
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

static void setSquare(char dst[APP_SQUARE_TEXT_LEN], char file, char rank)
{
    if (dst != NULL)
    {
        dst[0] = file;
        dst[1] = rank;
        dst[2] = '\0';
    }
}

static void setMoveText(char dst[APP_MOVE_TEXT_LEN], const char * src)
{
    copyText(dst, APP_MOVE_TEXT_LEN, src);
}

static esp_err_t initNvs(void)
{
    esp_err_t err;

    err = nvs_flash_init();

    if ((err == ESP_ERR_NVS_NO_FREE_PAGES) ||
        (err == ESP_ERR_NVS_NEW_VERSION_FOUND))
    {
        err = nvs_flash_erase();

        if (err == ESP_OK)
        {
            err = nvs_flash_init();
        }
    }

    return err;
}

static esp_err_t setEapIdentity(void)
{
    size_t len;
    esp_err_t err = ESP_ERR_INVALID_ARG;

    len = boundedStringLength(EAP_IDENTITY_TXT, EAP_MAX_LEN + 1U);

    if ((len > 0U) && (len <= EAP_MAX_LEN))
    {
        err = esp_eap_client_set_identity(
            (const unsigned char *)EAP_IDENTITY_TXT,
            (int)len
        );
    }

    return err;
}

static esp_err_t setEapUsername(void)
{
    size_t len;
    esp_err_t err = ESP_ERR_INVALID_ARG;

    len = boundedStringLength(EAP_USERNAME_TXT, EAP_MAX_LEN + 1U);

    if ((len > 0U) && (len <= EAP_MAX_LEN))
    {
        err = esp_eap_client_set_username(
            (const unsigned char *)EAP_USERNAME_TXT,
            (int)len
        );
    }

    return err;
}

static esp_err_t setEapPassword(void)
{
    size_t len;
    esp_err_t err = ESP_ERR_INVALID_ARG;

    len = boundedStringLength(EAP_PASSWORD_TXT, EAP_MAX_LEN + 1U);

    if ((len > 0U) && (len <= EAP_MAX_LEN))
    {
        err = esp_eap_client_set_password(
            (const unsigned char *)EAP_PASSWORD_TXT,
            (int)len
        );
    }

    return err;
}

static esp_err_t initEnterpriseAuth(void)
{
    esp_err_t err;

    err = setEapIdentity();

    if (err == ESP_OK)
    {
        err = setEapUsername();
    }

    if (err == ESP_OK)
    {
        err = setEapPassword();
    }

    if (err == ESP_OK)
    {
        err = esp_wifi_sta_enterprise_enable();
    }

    return err;
}

static void eventHandler(void * arg, esp_event_base_t base, int32_t id, void * data)
{
    esp_err_t err;

    (void)arg;

    if ((base == WIFI_EVENT) && (id == WIFI_EVENT_STA_START))
    {
        err = esp_wifi_connect();

        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "WiFi connect error: %ld", (long)err);
        }
    }
    else if ((base == WIFI_EVENT) && (id == WIFI_EVENT_STA_DISCONNECTED))
    {
        wifiConnected = 0U;

        if (data != NULL)
        {
            const wifi_event_sta_disconnected_t * const event =
                (const wifi_event_sta_disconnected_t *)data;

            ESP_LOGE(
                TAG,
                "WiFi disconnected. Reason: %u",
                (unsigned int)event->reason
            );
        }

        err = esp_wifi_connect();

        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "WiFi reconnect error: %ld", (long)err);
        }
    }
    else if ((base == IP_EVENT) && (id == IP_EVENT_STA_GOT_IP))
    {
        wifiConnected = 1U;

        if (data != NULL)
        {
            const ip_event_got_ip_t * const event =
                (const ip_event_got_ip_t *)data;

            ESP_LOGI(TAG, "ESP32 IP: " IPSTR, IP2STR(&event->ip_info.ip));
        }

        err = startHttpServer();

        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "HTTP server start failed: %ld", (long)err);
        }
    }
    else
    {
        /* Event intentionally ignored. */
    }
}

esp_err_t serverNetworkInit(void)
{
    esp_err_t err;
    esp_netif_t * staNetif;
    esp_netif_t * apNetif;
    size_t apSsidLen;
    wifi_init_config_t initCfg = WIFI_INIT_CONFIG_DEFAULT();
    wifi_config_t staCfg = {0};
    wifi_config_t apCfg = {0};

    err = initNvs();

    if (err == ESP_OK)
    {
        err = esp_netif_init();
    }

    if (err == ESP_OK)
    {
        err = esp_event_loop_create_default();
    }

    if (err == ESP_OK)
    {
        staNetif = esp_netif_create_default_wifi_sta();

        if (staNetif == NULL)
        {
            err = ESP_FAIL;
        }
    }

    if (err == ESP_OK)
    {
        apNetif = esp_netif_create_default_wifi_ap();

        if (apNetif == NULL)
        {
            err = ESP_FAIL;
        }
    }

    if (err == ESP_OK)
    {
        err = esp_wifi_init(&initCfg);
    }

    if (err == ESP_OK)
    {
        err = esp_event_handler_instance_register(
            WIFI_EVENT,
            ESP_EVENT_ANY_ID,
            eventHandler,
            NULL,
            NULL
        );
    }

    if (err == ESP_OK)
    {
        err = esp_event_handler_instance_register(
            IP_EVENT,
            IP_EVENT_STA_GOT_IP,
            eventHandler,
            NULL,
            NULL
        );
    }

    if (err == ESP_OK)
    {
        copyStringToU8(staCfg.sta.ssid, sizeof(staCfg.sta.ssid), WIFI_SSID_TXT);
        staCfg.sta.threshold.authmode = WIFI_AUTH_WPA2_ENTERPRISE;

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

    if (err == ESP_OK)
    {
        err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    }

    if (err == ESP_OK)
    {
        err = esp_wifi_set_config(WIFI_IF_STA, &staCfg);
    }

    if (err == ESP_OK)
    {
        err = initEnterpriseAuth();
    }

    if (err == ESP_OK)
    {
        err = esp_wifi_set_config(WIFI_IF_AP, &apCfg);
    }

    if (err == ESP_OK)
    {
        err = esp_wifi_start();
    }

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

        err = httpd_start(&httpServer, &config);

        if (err == ESP_OK)
        {
            err = httpd_register_uri_handler(httpServer, &rootUri);
        }

        if (err == ESP_OK)
        {
            ESP_LOGI(TAG, "Local HTTP server started on port %u", (unsigned int)config.server_port);
        }
    }

    return err;
}

static esp_err_t rootHandler(httpd_req_t * req)
{
    char html[LOCAL_HTML_BUF_LEN];
    char fen[FEN_TEXT_LEN];
    char piece[STATE_TEXT_LEN];
    char best[APP_MOVE_TEXT_LEN];
    char legal[LEGAL_TEXT_LEN];
    int len;
    esp_err_t err = ESP_ERR_INVALID_ARG;

    if (req != NULL)
    {
        if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(1000U)) == pdPASS)
        {
            copyText(fen, sizeof(fen), stateFen);
            copyText(piece, sizeof(piece), statePiece);
            copyText(best, sizeof(best), stateBest);
            copyText(legal, sizeof(legal), stateLegal);
            (void)xSemaphoreGive(stateMutex);
        }
        else
        {
            copyText(fen, sizeof(fen), "LOCK_ERROR");
            copyText(piece, sizeof(piece), "LOCK_ERROR");
            copyText(best, sizeof(best), "-----");
            copyText(legal, sizeof(legal), "-");
        }

        len = snprintf(
            html,
            sizeof(html),
            "<!DOCTYPE html><html lang=\"pt-BR\"><head><meta charset=\"UTF-8\">"
            "<meta http-equiv=\"refresh\" content=\"2\">"
            "<title>Xadrez ESP32-S3</title>"
            "<style>"
            "body{font-family:Arial,sans-serif;background:#f4f4f9;margin:24px;color:#111}"
            "h1{margin:0 0 16px 0}"
            ".layout{display:flex;gap:24px;align-items:flex-start;flex-wrap:wrap}"
            ".board{display:grid;grid-template-columns:repeat(8,58px);grid-template-rows:repeat(8,58px);"
            "border:3px solid #333;box-shadow:0 2px 12px #999}"
            ".sq{width:58px;height:58px;display:flex;align-items:center;justify-content:center;"
            "font-size:38px;position:relative;box-sizing:border-box}"
            ".light{background:#f0d9b5}.dark{background:#b58863}"
            ".legal:after{content:\"\";width:20px;height:20px;border-radius:50%%;background:#fff;"
            "opacity:.88;position:absolute;box-shadow:0 0 6px #111}"
            ".best{outline:5px solid #12b312;outline-offset:-5px;background:#77cc77!important}"
            ".info{min-width:360px;max-width:760px}"
            ".box{background:#fff;padding:14px;margin:0 0 12px 0;border:1px solid #ccc;border-radius:8px}"
            ".fen{font-family:monospace;word-break:break-all}"
            ".legend span{display:inline-block;margin-right:14px}"
            ".dot{display:inline-block;width:16px;height:16px;border-radius:50%%;background:#fff;border:1px solid #333;vertical-align:middle}"
            ".green{display:inline-block;width:16px;height:16px;background:#77cc77;border:2px solid #12b312;vertical-align:middle}"
            "</style></head><body>"
            "<h1>Xadrez físico ESP32-S3</h1>"
            "<div class=\"layout\"><div id=\"board\" class=\"board\"></div>"
            "<div class=\"info\">"
            "<div class=\"box\"><b>Peça:</b> <span id=\"piece\"></span></div>"
            "<div class=\"box\"><b>FEN:</b><br><span id=\"fen\" class=\"fen\"></span></div>"
            "<div class=\"box\"><b>Casas válidas:</b> <span id=\"legal\"></span></div>"
            "<div class=\"box\"><b>Melhor jogada Lichess/Stockfish:</b> <span id=\"best\"></span></div>"
            "<div class=\"box legend\"><b>Legenda:</b><br>"
            "<span><i class=\"dot\"></i> casa válida</span>"
            "<span><i class=\"green\"></i> melhor jogada</span>"
            "</div></div></div>"
            "<script>"
            "const fen='%s';"
            "const legalText='%s';"
            "const best='%s';"
            "const piece='%s';"
            "const symbols={P:'♙',N:'♘',B:'♗',R:'♖',Q:'♕',K:'♔',p:'♟',n:'♞',b:'♝',r:'♜',q:'♛',k:'♚'};"
            "const legalSet=new Set(legalText==='-'?[]:legalText.split(','));"
            "const bestFrom=best.length>=4?best.substring(0,2):'';"
            "const bestTo=best.length>=4?best.substring(2,4):'';"
            "const cells={};"
            "function parseFen(f){"
            "let part=f.split(' ')[0];let rank=7;let file=0;"
            "for(let i=0;i<part.length;i++){"
            "let c=part[i];"
            "if(c==='/'){rank--;file=0;}"
            "else if(c>='1'&&c<='8'){file+=parseInt(c,10);}"
            "else{let sq=String.fromCharCode(97+file)+(rank+1);cells[sq]=c;file++;}"
            "}"
            "}"
            "function draw(){"
            "parseFen(fen);"
            "document.getElementById('fen').innerText=fen;"
            "document.getElementById('legal').innerText=legalText;"
            "document.getElementById('best').innerText=best;"
            "document.getElementById('piece').innerText=piece;"
            "const b=document.getElementById('board');"
            "for(let r=7;r>=0;r--){"
            "for(let f=0;f<8;f++){"
            "let sq=String.fromCharCode(97+f)+(r+1);"
            "let d=document.createElement('div');"
            "d.className='sq '+(((r+f)%%2===0)?'dark':'light');"
            "if(legalSet.has(sq)){d.className+=' legal';}"
            "if((sq===bestFrom)||(sq===bestTo)){d.className+=' best';}"
            "d.innerText=symbols[cells[sq]]||'';"
            "b.appendChild(d);"
            "}"
            "}"
            "}"
            "draw();"
            "</script></body></html>",
            fen,
            legal,
            best,
            piece
        );

        if ((len > 0) && ((size_t)len < sizeof(html)))
        {
            err = httpd_resp_set_type(req, "text/html");

            if (err == ESP_OK)
            {
                err = httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
            }
        }
        else
        {
            err = httpd_resp_send_500(req);
        }
    }

    return err;
}
static void updateState(const char * fen,
                        const char * piece,
                        const char * best,
                        const char * legal)
{
    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(1000U)) == pdPASS)
    {
        copyText(stateFen, sizeof(stateFen), fen);
        copyText(statePiece, sizeof(statePiece), piece);
        copyText(stateBest, sizeof(stateBest), best);
        copyText(stateLegal, sizeof(stateLegal), legal);
        (void)xSemaphoreGive(stateMutex);
    }
}

static esp_err_t buildLichessUrl(char * url, size_t urlLen)
{
    int len;
    esp_err_t err = ESP_ERR_INVALID_ARG;

    if ((url != NULL) && (urlLen > 0U))
    {
        len = snprintf(
            url,
            urlLen,
            "%s%s%s",
            LICHESS_CLOUD_EVAL_URL,
            FEN_WITH_KNIGHT_ENCODED,
            LICHESS_URL_SUFFIX
        );

        if ((len > 0) && ((size_t)len < urlLen))
        {
            err = ESP_OK;
        }
    }

    return err;
}

static esp_err_t httpsGet(const char * url, char * response, size_t responseLen)
{
    esp_err_t err = ESP_FAIL;
    esp_http_client_handle_t client = NULL;
    int headerLen = 0;
    int readLen = 0;
    int statusCode = 0;

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 15000,
        .crt_bundle_attach = esp_crt_bundle_attach
    };

    if ((url != NULL) && (response != NULL) && (responseLen > 0U))
    {
        response[0] = '\0';

        client = esp_http_client_init(&config);

        if (client != NULL)
        {
            err = esp_http_client_open(client, 0);

            if (err == ESP_OK)
            {
                headerLen = esp_http_client_fetch_headers(client);
                statusCode = esp_http_client_get_status_code(client);

                ESP_LOGI(TAG, "HTTP status: %d", statusCode);
                ESP_LOGI(TAG, "HTTP header length: %d", headerLen);

                readLen = esp_http_client_read_response(
                    client,
                    response,
                    (int)(responseLen - 1U)
                );

                ESP_LOGI(TAG, "HTTP read length: %d", readLen);

                if (readLen >= 0)
                {
                    response[readLen] = '\0';
                    ESP_LOGI(TAG, "HTTP response: %s", response);

                    if (statusCode == 200)
                    {
                        err = ESP_OK;
                    }
                    else
                    {
                        err = ESP_FAIL;
                    }
                }
                else
                {
                    ESP_LOGE(TAG, "HTTP read failed");
                    err = ESP_FAIL;
                }
            }
            else
            {
                ESP_LOGE(TAG, "HTTP open failed: %ld", (long)err);
            }

            (void)esp_http_client_close(client);
            (void)esp_http_client_cleanup(client);
        }
    }

    return err;
}


static uint8_t parseBestMoveFromJson(const char * json, char bestMove[APP_MOVE_TEXT_LEN])
{
    uint8_t found = 0U;
    size_t i = 0U;
    size_t j = 0U;
    size_t k = 0U;
    static const char key[] = "\"moves\"";

    if ((json != NULL) && (bestMove != NULL))
    {
        bestMove[0] = '-';
        bestMove[1] = '-';
        bestMove[2] = '-';
        bestMove[3] = '-';
        bestMove[4] = '\0';

        while (json[i] != '\0')
        {
            j = 0U;

            while ((key[j] != '\0') && (json[i + j] == key[j]))
            {
                j++;
            }

            if (key[j] == '\0')
            {
                i += j;

                while ((json[i] != '\0') && (json[i] != ':'))
                {
                    i++;
                }

                if (json[i] == ':')
                {
                    i++;
                }

                while ((json[i] == ' ') || (json[i] == '	'))
                {
                    i++;
                }

                if (json[i] == '"')
                {
                    i++;
                }

                k = 0U;

                while ((json[i] != '\0') &&
                       (json[i] != ' ') &&
                       (json[i] != '"') &&
                       (k < (APP_MOVE_TEXT_LEN - 1U)))
                {
                    bestMove[k] = json[i];
                    k++;
                    i++;
                }

                bestMove[k] = '\0';

                if ((k >= 4U) &&
                    (bestMove[0] >= 'a') && (bestMove[0] <= 'h') &&
                    (bestMove[1] >= '1') && (bestMove[1] <= '8') &&
                    (bestMove[2] >= 'a') && (bestMove[2] <= 'h') &&
                    (bestMove[3] >= '1') && (bestMove[3] <= '8'))
                {
                    found = 1U;
                }

                break;
            }

            i++;
        }
    }

    return found;
}


static esp_err_t queryLichessBestMove(char bestMove[APP_MOVE_TEXT_LEN])
{
    char url[HTTPS_URL_LEN];
    char response[HTTPS_RESPONSE_LEN];
    esp_err_t err;

    setMoveText(bestMove, "-----");

    err = buildLichessUrl(url, sizeof(url));

    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "Lichess URL: %s", url);
        err = httpsGet(url, response, sizeof(response));
    }

    if (err == ESP_OK)
    {
        if (parseBestMoveFromJson(response, bestMove) == 0U)
        {
            ESP_LOGE(TAG, "Could not parse best move from Lichess response");
            err = ESP_FAIL;
        }
        else
        {
            ESP_LOGI(TAG, "Parsed best move: %s", bestMove);
        }
    }

    return err;
}


static void buildKnightLiftedLedCommand(uint32_t sequence,
                                        const char bestMove[APP_MOVE_TEXT_LEN],
                                        led_command_t * command)
{
    if (command != NULL)
    {
        command->sequence = sequence;
        command->clear = 0U;
        command->legalCount = 2U;

        setSquare(command->legal[0], 'f', '3');
        setSquare(command->legal[1], 'h', '3');

        if ((bestMove != NULL) &&
            (bestMove[0] >= 'a') && (bestMove[0] <= 'h') &&
            (bestMove[1] >= '1') && (bestMove[1] <= '8') &&
            (bestMove[2] >= 'a') && (bestMove[2] <= 'h') &&
            (bestMove[3] >= '1') && (bestMove[3] <= '8'))
        {
            command->bestValid = 1U;
            setSquare(command->bestFrom, bestMove[0], bestMove[1]);
            setSquare(command->bestTo, bestMove[2], bestMove[3]);
        }
        else
        {
            command->bestValid = 0U;
            setSquare(command->bestFrom, 'g', '1');
            setSquare(command->bestTo, 'f', '3');
        }
    }
}

static void buildClearLedCommand(uint32_t sequence, led_command_t * command)
{
    if (command != NULL)
    {
        command->sequence = sequence;
        command->clear = 1U;
        command->bestValid = 0U;
        command->legalCount = 0U;
        setSquare(command->bestFrom, 'g', '1');
        setSquare(command->bestTo, 'g', '1');
    }
}

void serverTask(void * parameters)
{
    sensor_event_t event;
    led_command_t command;
    char bestMove[APP_MOVE_TEXT_LEN];
    BaseType_t status;
    esp_err_t err;

    (void)parameters;

    while (wifiConnected == 0U)
    {
        ESP_LOGI(TAG, "Waiting for WPA2 Enterprise connection...");
        vTaskDelay(pdMS_TO_TICKS(WIFI_WAIT_MS));
    }

    ESP_LOGI(TAG, "Network ready");

    for (;;)
    {
        status = xQueueReceive(sensorQueueRef, &event, portMAX_DELAY);

        if (status == pdPASS)
        {
            if (event.state == SENSOR_STATE_LIFTED)
            {
                ESP_LOGI(TAG, "Knight lifted. Querying Lichess Cloud Eval.");

                err = queryLichessBestMove(bestMove);

                if (err != ESP_OK)
                {
                    setMoveText(bestMove, "-----");
                    ESP_LOGE(TAG, "Lichess query failed: %ld", (long)err);
                }
                else
                {
                    ESP_LOGI(TAG, "Best move: %s", bestMove);
                }

                updateState(
                    FEN_NO_KNIGHT_TEXT,
                    "KNIGHT_LIFTED_FROM_G1",
                    bestMove,
                    "f3,h3"
                );

                buildKnightLiftedLedCommand(event.sequence, bestMove, &command);
                (void)xQueueSend(ledQueueRef, &command, portMAX_DELAY);
            }
            else
            {
                ESP_LOGI(TAG, "Knight returned to g1.");

                updateState(
                    FEN_WITH_KNIGHT_TEXT,
                    "KNIGHT_PRESENT_ON_G1",
                    "-----",
                    "-"
                );

                buildClearLedCommand(event.sequence, &command);
                (void)xQueueSend(ledQueueRef, &command, portMAX_DELAY);
            }
        }
    }
}
