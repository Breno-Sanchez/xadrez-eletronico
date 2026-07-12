#include "project_config.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

extern const uint8_t _binary_config_yaml_start[] asm("_binary_config_yaml_start");
extern const uint8_t _binary_config_yaml_end[] asm("_binary_config_yaml_end");

#define CONFIG_LINE_LEN (192U)

static project_config_t config;
static uint8_t configLoaded = 0U;

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

    if ((dst == NULL) || (dstLen == 0U))
    {
        return;
    }

    dst[0] = '\0';

    if (src == NULL)
    {
        return;
    }

    while ((src[index] != '\0') && (index < (dstLen - 1U)))
    {
        dst[index] = src[index];
        index++;
    }

    dst[index] = '\0';
}

static void setDefaults(project_config_t * cfg)
{
    if (cfg == NULL)
    {
        return;
    }

    (void)memset(cfg, 0, sizeof(*cfg));

    cfg->stockfish_enabled = 1U;
    copyText(cfg->stockfish_url, sizeof(cfg->stockfish_url), "https://stockfish.online/api/s/v2.php");
    cfg->stockfish_depth = 10U;
    cfg->stockfish_timeout_ms = 3500U;
    cfg->stockfish_response_bytes = 1536U;

    cfg->led_gpio = 38U;
    cfg->led_physical_count = 150U;
    cfg->led_brightness_percent = 30U;

    cfg->led_empty_rgb = (project_rgb_t){0U, 0U, 0U};
    cfg->led_piece_rgb = (project_rgb_t){0U, 0U, 18U};
    cfg->led_lifted_rgb = (project_rgb_t){0U, 0U, 28U};
    cfg->led_legal_rgb = (project_rgb_t){42U, 32U, 0U};
    cfg->led_best_rgb = (project_rgb_t){0U, 36U, 0U};
    cfg->led_invalid_rgb = (project_rgb_t){48U, 0U, 0U};
    cfg->led_check_rgb = (project_rgb_t){48U, 0U, 0U};
    cfg->led_draw_rgb = (project_rgb_t){42U, 32U, 0U};
}

static bool keyMatches(const char * line, const char * key, size_t * valueOffset)
{
    size_t pos = 0U;
    size_t keyLen;

    if ((line == NULL) || (key == NULL) || (valueOffset == NULL))
    {
        return false;
    }

    while ((line[pos] == ' ') || (line[pos] == '\t'))
    {
        pos++;
    }

    if ((line[pos] == '#') || (line[pos] == '\0'))
    {
        return false;
    }

    keyLen = boundedStringLength(key, 64U);

    if (strncmp(&line[pos], key, keyLen) != 0)
    {
        return false;
    }

    pos += keyLen;

    while ((line[pos] == ' ') || (line[pos] == '\t'))
    {
        pos++;
    }

    if (line[pos] != ':')
    {
        return false;
    }

    pos++;

    while ((line[pos] == ' ') || (line[pos] == '\t'))
    {
        pos++;
    }

    *valueOffset = pos;
    return true;
}

static void cleanScalar(char * text)
{
    size_t len;
    size_t start = 0U;

    if (text == NULL)
    {
        return;
    }

    while ((text[start] == ' ') || (text[start] == '\t'))
    {
        start++;
    }

    if (start > 0U)
    {
        (void)memmove(text, &text[start], boundedStringLength(&text[start], CONFIG_LINE_LEN) + 1U);
    }

    len = boundedStringLength(text, CONFIG_LINE_LEN);

    while ((len > 0U) &&
           ((text[len - 1U] == ' ') || (text[len - 1U] == '\t') ||
            (text[len - 1U] == '\r') || (text[len - 1U] == '\n')))
    {
        text[len - 1U] = '\0';
        len--;
    }

    if ((len >= 2U) && (text[0] == '"') && (text[len - 1U] == '"'))
    {
        text[len - 1U] = '\0';
        (void)memmove(text, &text[1], boundedStringLength(&text[1], CONFIG_LINE_LEN) + 1U);
    }
}

static bool readValue(const char * key, char * out, size_t outLen)
{
    const char * cfgText = (const char *)_binary_config_yaml_start;
    size_t cfgLen = (size_t)(_binary_config_yaml_end - _binary_config_yaml_start);
    size_t pos = 0U;
    char line[CONFIG_LINE_LEN];

    if ((key == NULL) || (out == NULL) || (outLen == 0U))
    {
        return false;
    }

    out[0] = '\0';

    while (pos < cfgLen)
    {
        size_t lineLen = 0U;
        size_t valueOffset = 0U;

        while ((pos < cfgLen) &&
               (cfgText[pos] != '\n') &&
               (lineLen < (sizeof(line) - 1U)))
        {
            line[lineLen] = cfgText[pos];
            lineLen++;
            pos++;
        }

        while ((pos < cfgLen) && (cfgText[pos] != '\n'))
        {
            pos++;
        }

        if ((pos < cfgLen) && (cfgText[pos] == '\n'))
        {
            pos++;
        }

        line[lineLen] = '\0';

        for (size_t index = 0U; line[index] != '\0'; index++)
        {
            if (line[index] == '#')
            {
                line[index] = '\0';
                break;
            }
        }

        if (keyMatches(line, key, &valueOffset) != false)
        {
            copyText(out, outLen, &line[valueOffset]);
            cleanScalar(out);
            return true;
        }
    }

    return false;
}

static uint32_t parseUint(const char * text, uint32_t fallback)
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
            uint32_t digit = (uint32_t)(text[index] - '0');
            value = (value * 10U) + digit;
            any = true;
        }
        else if ((text[index] != ' ') && (text[index] != '\t'))
        {
            break;
        }
    }

    return (any != false) ? value : fallback;
}

static uint8_t parseBool(const char * text, uint8_t fallback)
{
    uint8_t value = fallback;

    if (text != NULL)
    {
        if ((strcmp(text, "true") == 0) || (strcmp(text, "1") == 0) ||
            (strcmp(text, "yes") == 0) || (strcmp(text, "on") == 0))
        {
            value = 1U;
        }
        else if ((strcmp(text, "false") == 0) || (strcmp(text, "0") == 0) ||
                 (strcmp(text, "no") == 0) || (strcmp(text, "off") == 0))
        {
            value = 0U;
        }
    }

    return value;
}

static uint8_t clampU8(uint32_t value)
{
    return (value > 255U) ? 255U : (uint8_t)value;
}

static void parseRgbValue(const char * text, project_rgb_t * rgb)
{
    uint32_t values[3] = {0U, 0U, 0U};
    uint8_t count = 0U;
    uint32_t current = 0U;
    bool reading = false;

    if ((text == NULL) || (rgb == NULL))
    {
        return;
    }

    for (size_t index = 0U; (text[index] != '\0') && (count < 3U); index++)
    {
        if ((text[index] >= '0') && (text[index] <= '9'))
        {
            current = (current * 10U) + (uint32_t)(text[index] - '0');
            reading = true;
        }
        else if (reading != false)
        {
            values[count] = current;
            count++;
            current = 0U;
            reading = false;
        }
    }

    if ((reading != false) && (count < 3U))
    {
        values[count] = current;
        count++;
    }

    if (count == 3U)
    {
        rgb->r = clampU8(values[0]);
        rgb->g = clampU8(values[1]);
        rgb->b = clampU8(values[2]);
    }
}

static void loadScalarConfig(project_config_t * cfg)
{
    char value[CONFIG_LINE_LEN];
    uint32_t tmp;

    if (cfg == NULL)
    {
        return;
    }

    if (readValue("stockfish_enabled", value, sizeof(value)) != false)
    {
        cfg->stockfish_enabled = parseBool(value, cfg->stockfish_enabled);
    }

    if (readValue("stockfish_url", value, sizeof(value)) != false)
    {
        copyText(cfg->stockfish_url, sizeof(cfg->stockfish_url), value);
    }

    if (readValue("stockfish_depth", value, sizeof(value)) != false)
    {
        tmp = parseUint(value, cfg->stockfish_depth);
        if (tmp < 1U)
        {
            tmp = 1U;
        }
        if (tmp > 15U)
        {
            tmp = 15U;
        }
        cfg->stockfish_depth = (uint8_t)tmp;
    }

    if (readValue("stockfish_timeout_ms", value, sizeof(value)) != false)
    {
        cfg->stockfish_timeout_ms = parseUint(value, cfg->stockfish_timeout_ms);
    }

    if (readValue("stockfish_response_bytes", value, sizeof(value)) != false)
    {
        cfg->stockfish_response_bytes = parseUint(value, cfg->stockfish_response_bytes);
    }

    if (readValue("led_gpio", value, sizeof(value)) != false)
    {
        cfg->led_gpio = parseUint(value, cfg->led_gpio);
    }

    if (readValue("led_physical_count", value, sizeof(value)) != false)
    {
        cfg->led_physical_count = parseUint(value, cfg->led_physical_count);
    }

    if (readValue("led_brightness_percent", value, sizeof(value)) != false)
    {
        tmp = parseUint(value, cfg->led_brightness_percent);
        cfg->led_brightness_percent = (tmp > 100U) ? 100U : (uint8_t)tmp;
    }
}

static void loadRgbConfig(project_config_t * cfg)
{
    char value[CONFIG_LINE_LEN];

    if (cfg == NULL)
    {
        return;
    }

    if (readValue("led_empty_rgb", value, sizeof(value)) != false) parseRgbValue(value, &cfg->led_empty_rgb);
    if (readValue("led_piece_rgb", value, sizeof(value)) != false) parseRgbValue(value, &cfg->led_piece_rgb);
    if (readValue("led_lifted_rgb", value, sizeof(value)) != false) parseRgbValue(value, &cfg->led_lifted_rgb);
    if (readValue("led_legal_rgb", value, sizeof(value)) != false) parseRgbValue(value, &cfg->led_legal_rgb);
    if (readValue("led_best_rgb", value, sizeof(value)) != false) parseRgbValue(value, &cfg->led_best_rgb);
    if (readValue("led_invalid_rgb", value, sizeof(value)) != false) parseRgbValue(value, &cfg->led_invalid_rgb);
    if (readValue("led_check_rgb", value, sizeof(value)) != false) parseRgbValue(value, &cfg->led_check_rgb);
    if (readValue("led_draw_rgb", value, sizeof(value)) != false) parseRgbValue(value, &cfg->led_draw_rgb);
}

const project_config_t * projectConfigGet(void)
{
    if (configLoaded == 0U)
    {
        setDefaults(&config);
        loadScalarConfig(&config);
        loadRgbConfig(&config);
        configLoaded = 1U;
    }

    return &config;
}
