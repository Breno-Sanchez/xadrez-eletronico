#include "runtime_config.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "nvs.h"

#define RUNTIME_CONFIG_NAMESPACE "runtime_cfg"
#define RUNTIME_CLOCK_DEFAULT_SECONDS (300U)
#define RUNTIME_CLOCK_MAX_SECONDS     (21600U)
#define RUNTIME_CLOCK_BONUS_MAX_SEC   (600U)

static runtime_config_t runtimeConfig;
static uint8_t runtimeConfigLoaded = 0U;

static const char * const colorKeys[RUNTIME_LED_COLOR_COUNT][3] = {
    { "empty_r",   "empty_g",   "empty_b"   },
    { "piece_r",   "piece_g",   "piece_b"   },
    { "lifted_r",  "lifted_g",  "lifted_b"  },
    { "legal_r",   "legal_g",   "legal_b"   },
    { "best_r",    "best_g",    "best_b"    },
    { "invalid_r", "invalid_g", "invalid_b" },
    { "check_r",   "check_g",   "check_b"   },
    { "draw_r",    "draw_g",    "draw_b"    }
};

static uint8_t clampStockfishDepth(uint8_t depth)
{
    uint8_t value = depth;

    if (value < 10U)
    {
        value = 10U;
    }
    else if (value > 15U)
    {
        value = 15U;
    }

    return value;
}

static uint16_t clampClockSeconds(uint16_t seconds, uint16_t maxSeconds)
{
    return (seconds > maxSeconds) ? maxSeconds : seconds;
}

static void setDefaults(runtime_config_t * cfg)
{
    const project_config_t * projectCfg;

    if (cfg == NULL)
    {
        return;
    }

    projectCfg = projectConfigGet();

    (void)memset(cfg, 0, sizeof(*cfg));

    cfg->invalid_position_infractions_enabled = 1U;
    cfg->led_empty_enabled = 0U;
    cfg->stockfish_enabled = (projectCfg->stockfish_enabled != 0U) ? 1U : 0U;
    cfg->stockfish_depth = clampStockfishDepth(projectCfg->stockfish_depth);
    cfg->clock_initial_seconds = RUNTIME_CLOCK_DEFAULT_SECONDS;
    cfg->clock_bonus_seconds = 0U;
    cfg->led_brightness_percent = projectCfg->led_brightness_percent;

    cfg->led_empty_rgb = projectCfg->led_empty_rgb;
    cfg->led_piece_rgb = projectCfg->led_piece_rgb;
    cfg->led_lifted_rgb = projectCfg->led_lifted_rgb;
    cfg->led_legal_rgb = projectCfg->led_legal_rgb;
    cfg->led_best_rgb = projectCfg->led_best_rgb;
    cfg->led_invalid_rgb = projectCfg->led_invalid_rgb;
    cfg->led_check_rgb = projectCfg->led_check_rgb;
    cfg->led_draw_rgb = projectCfg->led_draw_rgb;
}

static project_rgb_t * mutableColor(runtime_config_t * cfg, runtime_led_color_id_t colorId)
{
    project_rgb_t * color = NULL;

    if (cfg != NULL)
    {
        switch (colorId)
        {
            case RUNTIME_LED_COLOR_EMPTY:   color = &cfg->led_empty_rgb; break;
            case RUNTIME_LED_COLOR_PIECE:   color = &cfg->led_piece_rgb; break;
            case RUNTIME_LED_COLOR_LIFTED:  color = &cfg->led_lifted_rgb; break;
            case RUNTIME_LED_COLOR_LEGAL:   color = &cfg->led_legal_rgb; break;
            case RUNTIME_LED_COLOR_BEST:    color = &cfg->led_best_rgb; break;
            case RUNTIME_LED_COLOR_INVALID: color = &cfg->led_invalid_rgb; break;
            case RUNTIME_LED_COLOR_CHECK:   color = &cfg->led_check_rgb; break;
            case RUNTIME_LED_COLOR_DRAW:    color = &cfg->led_draw_rgb; break;
            default: break;
        }
    }

    return color;
}

static project_rgb_t readColor(const runtime_config_t * cfg, runtime_led_color_id_t colorId)
{
    project_rgb_t color = {0U, 0U, 0U};

    if (cfg != NULL)
    {
        switch (colorId)
        {
            case RUNTIME_LED_COLOR_EMPTY:   color = cfg->led_empty_rgb; break;
            case RUNTIME_LED_COLOR_PIECE:   color = cfg->led_piece_rgb; break;
            case RUNTIME_LED_COLOR_LIFTED:  color = cfg->led_lifted_rgb; break;
            case RUNTIME_LED_COLOR_LEGAL:   color = cfg->led_legal_rgb; break;
            case RUNTIME_LED_COLOR_BEST:    color = cfg->led_best_rgb; break;
            case RUNTIME_LED_COLOR_INVALID: color = cfg->led_invalid_rgb; break;
            case RUNTIME_LED_COLOR_CHECK:   color = cfg->led_check_rgb; break;
            case RUNTIME_LED_COLOR_DRAW:    color = cfg->led_draw_rgb; break;
            default: break;
        }
    }

    return color;
}

static void loadU8(nvs_handle_t handle, const char * key, uint8_t * value)
{
    uint8_t tmp;

    if ((key != NULL) && (value != NULL))
    {
        if (nvs_get_u8(handle, key, &tmp) == ESP_OK)
        {
            *value = tmp;
        }
    }
}

static esp_err_t saveU8(nvs_handle_t handle, const char * key, uint8_t value)
{
    if (key == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    return nvs_set_u8(handle, key, value);
}

static void loadU16(nvs_handle_t handle, const char * key, uint16_t * value)
{
    uint16_t tmp;

    if ((key != NULL) && (value != NULL))
    {
        if (nvs_get_u16(handle, key, &tmp) == ESP_OK)
        {
            *value = tmp;
        }
    }
}

static esp_err_t saveU16(nvs_handle_t handle, const char * key, uint16_t value)
{
    if (key == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    return nvs_set_u16(handle, key, value);
}

static void loadColor(nvs_handle_t handle, runtime_led_color_id_t colorId)
{
    project_rgb_t * color;

    if ((uint32_t)colorId >= (uint32_t)RUNTIME_LED_COLOR_COUNT)
    {
        return;
    }

    color = mutableColor(&runtimeConfig, colorId);
    if (color == NULL)
    {
        return;
    }

    loadU8(handle, colorKeys[colorId][0], &color->r);
    loadU8(handle, colorKeys[colorId][1], &color->g);
    loadU8(handle, colorKeys[colorId][2], &color->b);
}

static esp_err_t saveColor(nvs_handle_t handle, runtime_led_color_id_t colorId)
{
    project_rgb_t color;
    esp_err_t err = ESP_OK;

    if ((uint32_t)colorId >= (uint32_t)RUNTIME_LED_COLOR_COUNT)
    {
        return ESP_ERR_INVALID_ARG;
    }

    color = readColor(&runtimeConfig, colorId);

    err = saveU8(handle, colorKeys[colorId][0], color.r);
    if (err == ESP_OK) err = saveU8(handle, colorKeys[colorId][1], color.g);
    if (err == ESP_OK) err = saveU8(handle, colorKeys[colorId][2], color.b);

    return err;
}

const runtime_config_t * runtimeConfigGet(void)
{
    if (runtimeConfigLoaded == 0U)
    {
        setDefaults(&runtimeConfig);
        runtimeConfigLoaded = 1U;
    }

    return &runtimeConfig;
}

esp_err_t runtimeConfigLoadFromNvs(void)
{
    nvs_handle_t handle;
    esp_err_t err;

    setDefaults(&runtimeConfig);
    runtimeConfigLoaded = 1U;

    err = nvs_open(RUNTIME_CONFIG_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK)
    {
        return err;
    }

    loadU8(handle, "inv_inf", &runtimeConfig.invalid_position_infractions_enabled);
    loadU8(handle, "empty_on", &runtimeConfig.led_empty_enabled);
    loadU8(handle, "sf_on", &runtimeConfig.stockfish_enabled);
    loadU8(handle, "sf_depth", &runtimeConfig.stockfish_depth);
    runtimeConfig.stockfish_depth = clampStockfishDepth(runtimeConfig.stockfish_depth);
    loadU16(handle, "clk_init", &runtimeConfig.clock_initial_seconds);
    loadU16(handle, "clk_bonus", &runtimeConfig.clock_bonus_seconds);
    runtimeConfig.clock_initial_seconds = clampClockSeconds(runtimeConfig.clock_initial_seconds, RUNTIME_CLOCK_MAX_SECONDS);
    runtimeConfig.clock_bonus_seconds = clampClockSeconds(runtimeConfig.clock_bonus_seconds, RUNTIME_CLOCK_BONUS_MAX_SEC);
    loadU8(handle, "bright", &runtimeConfig.led_brightness_percent);

    if (runtimeConfig.led_brightness_percent > 100U)
    {
        runtimeConfig.led_brightness_percent = 100U;
    }

    for (uint8_t colorId = 0U; colorId < (uint8_t)RUNTIME_LED_COLOR_COUNT; colorId++)
    {
        loadColor(handle, (runtime_led_color_id_t)colorId);
    }

    nvs_close(handle);
    return ESP_OK;
}

esp_err_t runtimeConfigSave(void)
{
    nvs_handle_t handle;
    esp_err_t err;

    (void)runtimeConfigGet();

    err = nvs_open(RUNTIME_CONFIG_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        return err;
    }

    err = saveU8(handle, "inv_inf", runtimeConfig.invalid_position_infractions_enabled);
    if (err == ESP_OK) err = saveU8(handle, "empty_on", runtimeConfig.led_empty_enabled);
    if (err == ESP_OK) err = saveU8(handle, "sf_on", runtimeConfig.stockfish_enabled);
    if (err == ESP_OK) err = saveU8(handle, "sf_depth", runtimeConfig.stockfish_depth);
    if (err == ESP_OK) err = saveU16(handle, "clk_init", runtimeConfig.clock_initial_seconds);
    if (err == ESP_OK) err = saveU16(handle, "clk_bonus", runtimeConfig.clock_bonus_seconds);
    if (err == ESP_OK) err = saveU8(handle, "bright", runtimeConfig.led_brightness_percent);

    for (uint8_t colorId = 0U; (err == ESP_OK) && (colorId < (uint8_t)RUNTIME_LED_COLOR_COUNT); colorId++)
    {
        err = saveColor(handle, (runtime_led_color_id_t)colorId);
    }

    if (err == ESP_OK)
    {
        err = nvs_commit(handle);
    }

    nvs_close(handle);
    return err;
}

esp_err_t runtimeConfigReset(void)
{
    setDefaults(&runtimeConfig);
    runtimeConfigLoaded = 1U;
    return runtimeConfigSave();
}

esp_err_t runtimeConfigSetInvalidPositionInfractions(uint8_t enabled)
{
    (void)runtimeConfigGet();
    runtimeConfig.invalid_position_infractions_enabled = (enabled != 0U) ? 1U : 0U;
    return runtimeConfigSave();
}

esp_err_t runtimeConfigSetLedEmptyEnabled(uint8_t enabled)
{
    (void)runtimeConfigGet();
    runtimeConfig.led_empty_enabled = (enabled != 0U) ? 1U : 0U;
    return runtimeConfigSave();
}

esp_err_t runtimeConfigSetStockfishEnabled(uint8_t enabled)
{
    (void)runtimeConfigGet();
    runtimeConfig.stockfish_enabled = (enabled != 0U) ? 1U : 0U;
    return runtimeConfigSave();
}

esp_err_t runtimeConfigSetStockfishDepth(uint8_t depth)
{
    (void)runtimeConfigGet();
    runtimeConfig.stockfish_depth = clampStockfishDepth(depth);
    return runtimeConfigSave();
}

esp_err_t runtimeConfigSetClockInitialSeconds(uint16_t seconds)
{
    (void)runtimeConfigGet();
    runtimeConfig.clock_initial_seconds = clampClockSeconds(seconds, RUNTIME_CLOCK_MAX_SECONDS);
    return runtimeConfigSave();
}

esp_err_t runtimeConfigSetClockBonusSeconds(uint16_t seconds)
{
    (void)runtimeConfigGet();
    runtimeConfig.clock_bonus_seconds = clampClockSeconds(seconds, RUNTIME_CLOCK_BONUS_MAX_SEC);
    return runtimeConfigSave();
}

esp_err_t runtimeConfigSetLedBrightness(uint8_t percent)
{
    (void)runtimeConfigGet();
    runtimeConfig.led_brightness_percent = (percent > 100U) ? 100U : percent;
    return runtimeConfigSave();
}

esp_err_t runtimeConfigSetLedColor(runtime_led_color_id_t colorId, project_rgb_t color)
{
    project_rgb_t * dst;

    (void)runtimeConfigGet();

    dst = mutableColor(&runtimeConfig, colorId);
    if (dst == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    *dst = color;
    return runtimeConfigSave();
}

project_rgb_t runtimeConfigColor(runtime_led_color_id_t colorId)
{
    const runtime_config_t * cfg = runtimeConfigGet();
    return readColor(cfg, colorId);
}
