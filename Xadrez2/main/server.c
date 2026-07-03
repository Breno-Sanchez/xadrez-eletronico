#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_types.h"
#include "chess_logic.h"
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
#include "led.h"
#include "nvs_flash.h"
#include "server.h"

#define WIFI_SSID_TXT              "UTFPR-ALUNO"
#define EAP_IDENTITY_TXT           "a2342774"
#define EAP_USERNAME_TXT           "a2342774"
#define SOFTAP_SSID_TXT            "XADREZ_ESP"
#define SOFTAP_PASS_TXT            "xadrez12345"
#define SOFTAP_CHANNEL             (1U)
#define SOFTAP_MAX_CONNECTIONS     (2U)
#define EAP_PASSWORD_TXT           "mrmugu14"
#define EAP_MAX_LEN                (127U)
#define WIFI_WAIT_MS               (1000U)
#define LOCAL_HTML_BUF_LEN         (8192U)
#define STATE_TEXT_LEN             (32U)
#define FEN_TEXT_LEN               (128U)
#define LEGAL_TEXT_LEN             (64U)
#define PGN_TEXT_LEN               (1024U)
#define HTTPS_URL_LEN              (384U)
#define HTTPS_RESPONSE_LEN         (1024U)
#define LICHESS_CLOUD_EVAL_URL     "https://lichess.org/api/cloud-eval?fen="
#define LICHESS_URL_SUFFIX         "&multiPv=1"
#define MAX_LANCES_PLAYBACK        (300U)

typedef struct
{
    char lances[MAX_LANCES_PLAYBACK][5];
    int total_lances;
    int lance_atual;
    bool modo_playback_ativo;
} pgn_playback_t;

static const char * const TAG = "SERVER";

static QueueHandle_t sensorQueueRef = NULL;
static QueueHandle_t ledQueueRef = NULL;
static SemaphoreHandle_t stateMutex = NULL;
static httpd_handle_t httpServer = NULL;
static uint8_t wifiConnected = 0U;
static pgn_playback_t jogo_carregado = { .modo_playback_ativo = false };

static char stateFen[FEN_TEXT_LEN] = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
static char statePiece[STATE_TEXT_LEN] = "AGUARDANDO";
static char stateBest[APP_MOVE_TEXT_LEN] = "-----";
static char stateLegal[LEGAL_TEXT_LEN] = "-";
static char statePgn[PGN_TEXT_LEN] = "";

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
static esp_err_t api_leds_post_handler(httpd_req_t * req);
static esp_err_t api_pgn_post_handler(httpd_req_t * req);
static esp_err_t queryLichessBestMove(char bestMove[APP_MOVE_TEXT_LEN], const char * fen);
static esp_err_t buildLichessUrl(const char * fen, char * url, size_t urlLen);
static esp_err_t httpsGet(const char * url, char * response, size_t responseLen);
static uint8_t parseBestMoveFromJson(const char * json, char bestMove[APP_MOVE_TEXT_LEN]);
static void updateState(const char * fen,
                        const char * piece,
                        const char * best,
                        const char * legal,
                        const char * pgn);
static void buildOriginHintLedCommand(uint32_t sequence,
                                      const char square[APP_SQUARE_TEXT_LEN],
                                      led_command_t * command);
static void buildBestMoveLedCommand(uint32_t sequence,
                                    const char bestMove[APP_MOVE_TEXT_LEN],
                                    led_command_t * command);
static void buildClearLedCommand(uint32_t sequence, led_command_t * command);

static const httpd_uri_t rootUri = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = rootHandler,
    .user_ctx = NULL
};

static const httpd_uri_t ledsUri = {
    .uri = "/api/leds",
    .method = HTTP_POST,
    .handler = api_leds_post_handler,
    .user_ctx = NULL
};

static const httpd_uri_t pgnUri = {
    .uri = "/api/pgn",
    .method = HTTP_POST,
    .handler = api_pgn_post_handler,
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

static esp_err_t setEapIdentity(void)
{
    size_t len = boundedStringLength(EAP_IDENTITY_TXT, EAP_MAX_LEN + 1U);
    if ((len > 0U) && (len <= EAP_MAX_LEN))
    {
        return esp_eap_client_set_identity((const unsigned char *)EAP_IDENTITY_TXT, (int)len);
    }
    return ESP_ERR_INVALID_ARG;
}

static esp_err_t setEapUsername(void)
{
    size_t len = boundedStringLength(EAP_USERNAME_TXT, EAP_MAX_LEN + 1U);
    if ((len > 0U) && (len <= EAP_MAX_LEN))
    {
        return esp_eap_client_set_username((const unsigned char *)EAP_USERNAME_TXT, (int)len);
    }
    return ESP_ERR_INVALID_ARG;
}

static esp_err_t setEapPassword(void)
{
    size_t len = boundedStringLength(EAP_PASSWORD_TXT, EAP_MAX_LEN + 1U);
    if ((len > 0U) && (len <= EAP_MAX_LEN))
    {
        return esp_eap_client_set_password((const unsigned char *)EAP_PASSWORD_TXT, (int)len);
    }
    return ESP_ERR_INVALID_ARG;
}

static esp_err_t initEnterpriseAuth(void)
{
    esp_err_t err = setEapIdentity();
    if (err == ESP_OK) err = setEapUsername();
    if (err == ESP_OK) err = setEapPassword();
    if (err == ESP_OK) err = esp_wifi_sta_enterprise_enable();
    return err;
}

static void eventHandler(void * arg, esp_event_base_t base, int32_t id, void * data)
{
    esp_err_t err;
    (void)arg;

    if ((base == WIFI_EVENT) && (id == WIFI_EVENT_STA_START))
    {
        err = esp_wifi_connect();
        if (err != ESP_OK) ESP_LOGE(TAG, "WiFi connect error: %ld", (long)err);
    }
    else if ((base == WIFI_EVENT) && (id == WIFI_EVENT_STA_DISCONNECTED))
    {
        wifiConnected = 0U;
        if (data != NULL)
        {
            const wifi_event_sta_disconnected_t * const event = (const wifi_event_sta_disconnected_t *)data;
            ESP_LOGE(TAG, "WiFi disconnected. Reason: %u", (unsigned int)event->reason);
        }
        err = esp_wifi_connect();
        if (err != ESP_OK) ESP_LOGE(TAG, "WiFi reconnect error: %ld", (long)err);
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
    size_t apSsidLen;
    wifi_init_config_t initCfg = WIFI_INIT_CONFIG_DEFAULT();
    wifi_config_t staCfg = {0};
    wifi_config_t apCfg = {0};

    err = initNvs();
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

    if (err == ESP_OK) err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_STA, &staCfg);
    if (err == ESP_OK) err = initEnterpriseAuth();
    if (err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_AP, &apCfg);
    if (err == ESP_OK) err = esp_wifi_start();
    if (err == ESP_OK) err = startHttpServer();

    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "SoftAP SSID: %s", SOFTAP_SSID_TXT);
        ESP_LOGI(TAG, "SoftAP IP: 192.168.4.1");
    }

    return err;
}

static esp_err_t api_leds_post_handler(httpd_req_t * req)
{
    char buf[100];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1U);

    if (ret <= 0) return ESP_FAIL;
    buf[ret] = '\0';

    int intensidade = 80, r = 0, g = 48, b = 0;
    (void)sscanf(buf, "i=%d&r=%d&g=%d&b=%d", &intensidade, &r, &g, &b);
    led_atualizar_config((uint8_t)intensidade, (uint8_t)r, (uint8_t)g, (uint8_t)b);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"success\"}");
    return ESP_OK;
}

static esp_err_t api_pgn_post_handler(httpd_req_t * req)
{
    char * buf = malloc(req->content_len + 1U);
    if (buf == NULL) return ESP_ERR_NO_MEM;

    int ret = httpd_req_recv(req, buf, req->content_len);
    if (ret <= 0)
    {
        free(buf);
        return ESP_FAIL;
    }

    buf[ret] = '\0';

    int lance_idx = 0;
    char * token = strtok(buf, " ");

    while ((token != NULL) && (lance_idx < (int)MAX_LANCES_PLAYBACK))
    {
        strncpy(jogo_carregado.lances[lance_idx], token, 4U);
        jogo_carregado.lances[lance_idx][4] = '\0';
        lance_idx++;
        token = strtok(NULL, " ");
    }

    jogo_carregado.total_lances = lance_idx;
    jogo_carregado.lance_atual = 0;
    jogo_carregado.modo_playback_ativo = (lance_idx > 0);

    free(buf);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"partida_carregada\"}");
    return ESP_OK;
}

static esp_err_t startHttpServer(void)
{
    esp_err_t err = ESP_OK;
    if (httpServer == NULL)
    {
        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.stack_size = 12288U;

        err = httpd_start(&httpServer, &config);

        if (err == ESP_OK) err = httpd_register_uri_handler(httpServer, &rootUri);
        if (err == ESP_OK) err = httpd_register_uri_handler(httpServer, &ledsUri);
        if (err == ESP_OK) err = httpd_register_uri_handler(httpServer, &pgnUri);

        if (err == ESP_OK)
        {
            ESP_LOGI(TAG, "HTTP server started on port %u", (unsigned int)config.server_port);
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
    char pgn[PGN_TEXT_LEN];
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
            copyText(pgn, sizeof(pgn), statePgn);
            (void)xSemaphoreGive(stateMutex);
        }
        else
        {
            copyText(fen, sizeof(fen), "LOCK_ERROR");
            copyText(piece, sizeof(piece), "LOCK_ERROR");
            copyText(best, sizeof(best), "-----");
            copyText(legal, sizeof(legal), "-");
            copyText(pgn, sizeof(pgn), "LOCK_ERROR");
        }

        len = snprintf(
            html,
            sizeof(html),
            "<!DOCTYPE html><html lang=\"pt-BR\"><head><meta charset=\"UTF-8\">"
            "<meta http-equiv=\"refresh\" content=\"2\">"
            "<title>Xadrez ESP32</title>"
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
            ".fen,.pgn{font-family:monospace;word-break:break-all}"
            ".legend span{display:inline-block;margin-right:14px}"
            ".dot{display:inline-block;width:16px;height:16px;border-radius:50%%;background:#fff;border:1px solid #333;vertical-align:middle}"
            ".green{display:inline-block;width:16px;height:16px;background:#77cc77;border:2px solid #12b312;vertical-align:middle}"
            "</style></head><body>"
            "<h1>Xadrez fisico ESP32</h1>"
            "<div class=\"layout\"><div id=\"board\" class=\"board\"></div>"
            "<div class=\"info\">"
            "<div class=\"box\"><b>Peca:</b> <span id=\"piece\"></span></div>"
            "<div class=\"box\"><b>FEN:</b><br><span id=\"fen\" class=\"fen\"></span></div>"
            "<div class=\"box\"><b>PGN:</b><br><span id=\"pgn\" class=\"pgn\"></span></div>"
            "<div class=\"box\"><b>Casas validas:</b> <span id=\"legal\"></span></div>"
            "<div class=\"box\"><b>Melhor jogada Lichess:</b> <span id=\"best\"></span></div>"
            "<div class=\"box legend\"><b>Legenda:</b><br>"
            "<span><i class=\"dot\"></i> casa valida</span>"
            "<span><i class=\"green\"></i> melhor jogada</span>"
            "</div></div></div>"
            "<script>"
            "const fen='%s';"
            "const legalText='%s';"
            "const best='%s';"
            "const piece='%s';"
            "const pgnText='%s';"
            "const symbols={P:'\\u2659',N:'\\u2658',B:'\\u2657',R:'\\u2656',Q:'\\u2655',K:'\\u2654',"
            "p:'\\u265F',n:'\\u265E',b:'\\u265D',r:'\\u265C',q:'\\u265B',k:'\\u265A'};"
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
            "document.getElementById('pgn').innerText=pgnText;"
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
            piece,
            pgn
        );

        if ((len > 0) && ((size_t)len < sizeof(html)))
        {
            err = httpd_resp_set_type(req, "text/html");
            if (err == ESP_OK) err = httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
        }
        else
        {
            err = httpd_resp_send_500(req);
        }
    }
    return err;
}

static void updateState(const char * fen, const char * piece, const char * best, const char * legal, const char * pgn)
{
    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(1000U)) == pdPASS)
    {
        copyText(stateFen, sizeof(stateFen), fen);
        copyText(statePiece, sizeof(statePiece), piece);
        copyText(stateBest, sizeof(stateBest), best);
        copyText(stateLegal, sizeof(stateLegal), legal);
        copyText(statePgn, sizeof(statePgn), pgn);
        (void)xSemaphoreGive(stateMutex);
    }
}

static esp_err_t buildLichessUrl(const char * fen, char * url, size_t urlLen)
{
    size_t outIndex = 0U;
    esp_err_t err = ESP_ERR_INVALID_ARG;

    if ((fen == NULL) || (url == NULL) || (urlLen == 0U)) return err;

    outIndex = (size_t)snprintf(url, urlLen, "%s", LICHESS_CLOUD_EVAL_URL);

    for (size_t i = 0U; fen[i] != '\0'; i++)
    {
        char c = fen[i];
        int written = 0;

        if (c == ' ') written = snprintf(&url[outIndex], urlLen - outIndex, "%%20");
        else if (c == '/') written = snprintf(&url[outIndex], urlLen - outIndex, "%%2F");
        else
        {
            if (outIndex + 1U >= urlLen) return ESP_ERR_INVALID_SIZE;
            url[outIndex++] = c;
            written = 1;
        }
        if (written <= 0) return ESP_ERR_INVALID_SIZE;
        outIndex += (size_t)written;
    }

    if (outIndex + strlen(LICHESS_URL_SUFFIX) >= urlLen) return ESP_ERR_INVALID_SIZE;

    (void)strcpy(&url[outIndex], LICHESS_URL_SUFFIX);
    return ESP_OK;
}

static esp_err_t httpsGet(const char * url, char * response, size_t responseLen)
{
    esp_err_t err = ESP_FAIL;
    esp_http_client_handle_t client = NULL;
    int readLen = 0;
    int statusCode = 0;

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 15000,
        .crt_bundle_attach = esp_crt_bundle_attach
    };

    if ((url == NULL) || (response == NULL) || (responseLen == 0U)) return ESP_ERR_INVALID_ARG;

    response[0] = '\0';
    client = esp_http_client_init(&config);
    if (client == NULL) return ESP_FAIL;

    err = esp_http_client_open(client, 0);
    if (err == ESP_OK)
    {
        (void)esp_http_client_fetch_headers(client);
        statusCode = esp_http_client_get_status_code(client);
        readLen = esp_http_client_read_response(client, response, (int)(responseLen - 1U));

        if (readLen >= 0)
        {
            response[readLen] = '\0';
            if (statusCode == 200) err = ESP_OK;
            else err = ESP_FAIL;
        }
        else err = ESP_FAIL;
    }

    (void)esp_http_client_close(client);
    (void)esp_http_client_cleanup(client);
    return err;
}

static uint8_t parseBestMoveFromJson(const char * json, char bestMove[APP_MOVE_TEXT_LEN])
{
    uint8_t found = 0U;
    size_t i = 0U;
    static const char key[] = "\"moves\"";

    if ((json == NULL) || (bestMove == NULL)) return found;

    setMoveText(bestMove, "-----");

    while (json[i] != '\0')
    {
        size_t j = 0U;
        while ((key[j] != '\0') && (json[i + j] == key[j])) j++;

        if (key[j] == '\0')
        {
            size_t k = 0U;
            i += j;
            while ((json[i] != '\0') && (json[i] != ':')) i++;
            if (json[i] == ':') i++;
            while ((json[i] == ' ') || (json[i] == '\t')) i++;
            if (json[i] == '"') i++;

            while ((json[i] != '\0') && (json[i] != ' ') && (json[i] != '"') && (k < (APP_MOVE_TEXT_LEN - 1U)))
            {
                bestMove[k++] = json[i++];
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
    return found;
}

static esp_err_t queryLichessBestMove(char bestMove[APP_MOVE_TEXT_LEN], const char * fen)
{
    char url[HTTPS_URL_LEN];
    char response[HTTPS_RESPONSE_LEN];
    esp_err_t err;

    setMoveText(bestMove, "-----");

    if (wifiConnected == 0U)
    {
        ESP_LOGW(TAG, "Sem internet para consultar Lichess");
        return ESP_ERR_WIFI_NOT_CONNECT;
    }

    err = buildLichessUrl(fen, url, sizeof(url));
    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "Lichess URL: %s", url);
        err = httpsGet(url, response, sizeof(response));
    }

    if (err == ESP_OK)
    {
        if (parseBestMoveFromJson(response, bestMove) == 0U)
        {
            ESP_LOGE(TAG, "Nao foi possivel parsear melhor jogada");
            err = ESP_FAIL;
        }
    }
    return err;
}

static void buildOriginHintLedCommand(uint32_t sequence, const char square[APP_SQUARE_TEXT_LEN], led_command_t * command)
{
    if (command != NULL)
    {
        command->sequence = sequence;
        command->clear = 0U;
        command->bestValid = 0U;
        command->legalCount = 1U;
        copyText(command->legal[0], APP_SQUARE_TEXT_LEN, square);
        setSquare(command->bestFrom, 'a', '1');
        setSquare(command->bestTo, 'a', '1');
    }
}

static void buildBestMoveLedCommand(uint32_t sequence, const char bestMove[APP_MOVE_TEXT_LEN], led_command_t * command)
{
    if (command != NULL)
    {
        command->sequence = sequence;
        command->clear = 0U;
        command->legalCount = 0U;

        if ((bestMove[0] >= 'a') && (bestMove[0] <= 'h') &&
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
        setSquare(command->bestFrom, 'a', '1');
        setSquare(command->bestTo, 'a', '1');
    }
}

// ==============================================================================
// TASK PRINCIPAL: MOTOR DE XADREZ, INTEGRAÇÃO C/C++ E TRAVAMENTO DE ERRO
// ==============================================================================
void serverTask(void * parameters)
{
    sensor_event_t event;
    led_command_t command;
    char bestMove[APP_MOVE_TEXT_LEN];
    BaseType_t status;
    esp_err_t err;
    
    int lin_origem = -1;
    int col_origem = -1;

    // --- VARIÁVEIS DE TRAVAMENTO DE ERRO (LUZ VERMELHA) ---
    bool modo_erro = false;
    int err_lin_esperada = -1, err_col_esperada = -1;
    char casa_origem_str[APP_SQUARE_TEXT_LEN] = "";
    char casa_destino_str[APP_SQUARE_TEXT_LEN] = "";

    (void)parameters;

    ESP_LOGI(TAG, "Motor de xadrez iniciando...");
    inicializar_tabuleiro();
    inicializar_pgn();
    updateState(fenAtual, "AGUARDANDO", "-----", "-", pgn_atual);

    for (;;)
    {
        status = xQueueReceive(sensorQueueRef, &event, portMAX_DELAY);

        if (status != pdPASS) continue;

        int col_atual = event.square[0] - 'a';
        int lin_atual = '8' - event.square[1];

        // 1. ESTADO TRAVADO: Aguardando devolver a peça pro lugar certo
        if (modo_erro) {
            if (event.state == SENSOR_STATE_PRESENT) {
                if (lin_atual == err_lin_esperada && col_atual == err_col_esperada) {
                    ESP_LOGI(TAG, "Erro corrigido! Tabuleiro destravado.");
                    modo_erro = false;
                    lin_origem = -1; 
                    col_origem = -1;
                    
                    led_limpar_erro(); // Limpa as luzes vermelhas
                    updateState(fenAtual, "CORRIGIDO", "-----", "-", pgn_atual);
                } else {
                    ESP_LOGW(TAG, "Ainda errado! Devolva a peca para %s", casa_origem_str);
                }
            }
            continue; // Pula o fluxo normal até arrumar o erro
        }

        // 2. FLUXO NORMAL DO JOGO
        if (event.state == SENSOR_STATE_LIFTED)
        {
            ESP_LOGI(TAG, "Peca levantada em: %s", event.square);
            lin_origem = lin_atual;
            col_origem = col_atual;
            strcpy(casa_origem_str, event.square); // Guarda a origem pra caso der erro depois

            buildOriginHintLedCommand(event.sequence, event.square, &command);
            (void)xQueueSend(ledQueueRef, &command, portMAX_DELAY);

            updateState(fenAtual, "LEVANTADA", stateBest, event.square, pgn_atual);
        }
        else if ((event.state == SENSOR_STATE_PRESENT) && (lin_origem != -1))
        {
            ESP_LOGI(TAG, "Peca colocada em: %s", event.square);
            strcpy(casa_destino_str, event.square);

            // Pergunta ao Motor C++ se a jogada é válida nas regras do Xadrez
            if (validar_movimento(lin_origem, col_origem, lin_atual, col_atual))
            {
                // Move virtualmente a peça
                mover_peca(lin_origem, col_origem, lin_atual, col_atual);
                gerar_fen();
                ESP_LOGI(TAG, "FEN Atualizada: %s", fenAtual);

                setMoveText(bestMove, "-----");

                if (wifiConnected != 0U)
                {
                    err = queryLichessBestMove(bestMove, fenAtual);
                    if (err != ESP_OK) ESP_LOGW(TAG, "Consulta Lichess falhou: %ld", (long)err);
                }

                // Acende verde pra melhor jogada do Lichess
                buildBestMoveLedCommand(event.sequence, bestMove, &command);
                (void)xQueueSend(ledQueueRef, &command, portMAX_DELAY);

                updateState(fenAtual, "JOGADA_OK", bestMove, "-", pgn_atual);
                
                lin_origem = -1;
                col_origem = -1;
            }
            else
            {
                // JOGADA INVÁLIDA DETECTADA
                ESP_LOGW(TAG, "Jogada Invalida! Travando tabuleiro.");
                
                modo_erro = true;
                err_lin_esperada = lin_origem;
                err_col_esperada = col_origem;
                
                // Acende os LEDs vermelhos alertando o erro (usando a função criada no led.c)
                led_set_erro(casa_origem_str, casa_destino_str);

                updateState(fenAtual, "JOGADA_INVALIDA", "-----", "-", pgn_atual);
            }
        }
    }
}