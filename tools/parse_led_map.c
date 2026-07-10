#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define BOARD_FILE_COUNT       (8U)
#define BOARD_RANK_COUNT       (8U)
#define BOARD_SQUARE_COUNT     (64U)
#define LED_MAP_INVALID_INDEX  (0xFFFFU)
#define TEXT_BUFFER_SIZE       (8192U)
#define LOG_BUFFER_SIZE        (256U)

typedef enum
{
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR
} log_level_t;

static void host_log(log_level_t level, const char * fmt, ...)
{
    char line[LOG_BUFFER_SIZE];
    const char * prefix = "INFO";
    va_list args;
    int used;

    if (fmt == NULL)
    {
        return;
    }

    if (level == LOG_LEVEL_WARN)
    {
        prefix = "WARN";
    }
    else if (level == LOG_LEVEL_ERROR)
    {
        prefix = "ERROR";
    }

    used = snprintf(line, sizeof(line), "[%s] ", prefix);
    if ((used < 0) || ((size_t)used >= sizeof(line)))
    {
        return;
    }

    va_start(args, fmt);
    used += vsnprintf(&line[used], sizeof(line) - (size_t)used, fmt, args);
    va_end(args);

    if (used < 0)
    {
        return;
    }

    if ((size_t)used >= (sizeof(line) - 1U))
    {
        used = (int)(sizeof(line) - 2U);
    }

    line[used] = '\n';
    line[used + 1] = '\0';

    (void)write((level == LOG_LEVEL_ERROR) ? STDERR_FILENO : STDOUT_FILENO, line, (size_t)used + 1U);
}

static int read_file(const char * path, char ** out_text, size_t * out_size)
{
    int fd;
    struct stat st;
    char * buffer;
    size_t total = 0U;

    if ((path == NULL) || (out_text == NULL) || (out_size == NULL))
    {
        return -1;
    }

    *out_text = NULL;
    *out_size = 0U;

    fd = open(path, O_RDONLY);
    if (fd < 0)
    {
        host_log(LOG_LEVEL_ERROR, "open failed for %s: %s", path, strerror(errno));
        return -1;
    }

    if (fstat(fd, &st) != 0)
    {
        host_log(LOG_LEVEL_ERROR, "fstat failed for %s: %s", path, strerror(errno));
        (void)close(fd);
        return -1;
    }

    if (st.st_size <= 0)
    {
        host_log(LOG_LEVEL_ERROR, "empty input log: %s", path);
        (void)close(fd);
        return -1;
    }

    buffer = (char *)calloc((size_t)st.st_size + 1U, 1U);
    if (buffer == NULL)
    {
        host_log(LOG_LEVEL_ERROR, "out of memory while reading %s", path);
        (void)close(fd);
        return -1;
    }

    while (total < (size_t)st.st_size)
    {
        ssize_t got = read(fd, &buffer[total], (size_t)st.st_size - total);
        if (got < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            host_log(LOG_LEVEL_ERROR, "read failed for %s: %s", path, strerror(errno));
            free(buffer);
            (void)close(fd);
            return -1;
        }

        if (got == 0)
        {
            break;
        }

        total += (size_t)got;
    }

    buffer[total] = '\0';
    (void)close(fd);

    *out_text = buffer;
    *out_size = total;

    return 0;
}

static bool parse_square(const char * text, size_t * offset, uint8_t * file, uint8_t * rank)
{
    char f;
    char r;

    if ((text == NULL) || (offset == NULL) || (file == NULL) || (rank == NULL))
    {
        return false;
    }

    while ((text[*offset] == ' ') || (text[*offset] == '\t'))
    {
        (*offset)++;
    }

    f = text[*offset];
    r = text[*offset + 1U];

    if ((f < 'a') || (f > 'h') || (r < '1') || (r > '8'))
    {
        return false;
    }

    *file = (uint8_t)(f - 'a');
    *rank = (uint8_t)(r - '1');
    *offset += 2U;

    return true;
}

static bool parse_u32(const char * text, size_t * offset, uint32_t * value)
{
    uint32_t out = 0U;
    bool found = false;

    if ((text == NULL) || (offset == NULL) || (value == NULL))
    {
        return false;
    }

    while ((text[*offset] == ' ') || (text[*offset] == '\t'))
    {
        (*offset)++;
    }

    while (isdigit((unsigned char)text[*offset]) != 0)
    {
        uint32_t digit = (uint32_t)(text[*offset] - '0');
        out = (out * 10U) + digit;
        found = true;
        (*offset)++;
    }

    if (found == false)
    {
        return false;
    }

    *value = out;
    return true;
}

static uint32_t parse_accept_lines(const char * text, uint16_t map[BOARD_RANK_COUNT][BOARD_FILE_COUNT])
{
    const char marker[] = "MAP_ACCEPT";
    const size_t marker_len = sizeof(marker) - 1U;
    const char * cursor = text;
    uint32_t accepted = 0U;

    if ((text == NULL) || (map == NULL))
    {
        return 0U;
    }

    while ((cursor = strstr(cursor, marker)) != NULL)
    {
        size_t offset = marker_len;
        uint8_t file = 0U;
        uint8_t rank = 0U;
        uint32_t led = 0U;

        if ((parse_square(cursor, &offset, &file, &rank) == true) &&
            (parse_u32(cursor, &offset, &led) == true) &&
            (led <= UINT16_MAX))
        {
            map[rank][file] = (uint16_t)led;
            accepted++;
        }

        cursor += marker_len;
    }

    return accepted;
}

static int append_text(char * out, size_t out_size, size_t * used, const char * fmt, ...)
{
    va_list args;
    int written;

    if ((out == NULL) || (used == NULL) || (fmt == NULL) || (*used >= out_size))
    {
        return -1;
    }

    va_start(args, fmt);
    written = vsnprintf(&out[*used], out_size - *used, fmt, args);
    va_end(args);

    if ((written < 0) || ((size_t)written >= (out_size - *used)))
    {
        return -1;
    }

    *used += (size_t)written;
    return 0;
}

static int write_file(const char * path, const char * text, size_t size)
{
    int fd;
    size_t sent = 0U;

    if ((path == NULL) || (text == NULL))
    {
        return -1;
    }

    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
    {
        host_log(LOG_LEVEL_ERROR, "open for write failed for %s: %s", path, strerror(errno));
        return -1;
    }

    while (sent < size)
    {
        ssize_t done = write(fd, &text[sent], size - sent);
        if (done < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            host_log(LOG_LEVEL_ERROR, "write failed for %s: %s", path, strerror(errno));
            (void)close(fd);
            return -1;
        }

        sent += (size_t)done;
    }

    (void)close(fd);
    return 0;
}

static int write_json(const uint16_t map[BOARD_RANK_COUNT][BOARD_FILE_COUNT], const char * path)
{
    char out[TEXT_BUFFER_SIZE];
    size_t used = 0U;
    bool first = true;

    if ((map == NULL) || (path == NULL))
    {
        return -1;
    }

    if (append_text(out, sizeof(out), &used, "{\n") != 0)
    {
        return -1;
    }

    for (uint8_t file = 0U; file < BOARD_FILE_COUNT; file++)
    {
        for (uint8_t rank = 0U; rank < BOARD_RANK_COUNT; rank++)
        {
            if (map[rank][file] != LED_MAP_INVALID_INDEX)
            {
                if (append_text(out, sizeof(out), &used, "%s  \"%c%c\": %u", first ? "" : ",\n", (char)('a' + file), (char)('1' + rank), (unsigned int)map[rank][file]) != 0)
                {
                    return -1;
                }

                first = false;
            }
        }
    }

    if (append_text(out, sizeof(out), &used, "\n}\n") != 0)
    {
        return -1;
    }

    return write_file(path, out, used);
}

static int write_header(const uint16_t map[BOARD_RANK_COUNT][BOARD_FILE_COUNT], const char * path)
{
    char out[TEXT_BUFFER_SIZE];
    size_t used = 0U;

    if ((map == NULL) || (path == NULL))
    {
        return -1;
    }

    if (append_text(out, sizeof(out), &used,
        "#ifndef LED_MAP_GENERATED_H\n"
        "#define LED_MAP_GENERATED_H\n\n"
        "#include <stdint.h>\n\n"
        "#define LED_MAP_INVALID_INDEX (0xFFFFU)\n\n"
        "static const uint16_t ledMapGenerated[8][8] = {\n") != 0)
    {
        return -1;
    }

    for (uint8_t rank = 0U; rank < BOARD_RANK_COUNT; rank++)
    {
        if (append_text(out, sizeof(out), &used, "    { ") != 0)
        {
            return -1;
        }

        for (uint8_t file = 0U; file < BOARD_FILE_COUNT; file++)
        {
            if (map[rank][file] == LED_MAP_INVALID_INDEX)
            {
                if (append_text(out, sizeof(out), &used, "%s0xFFFFU", (file == 0U) ? "" : ", ") != 0)
                {
                    return -1;
                }
            }
            else
            {
                if (append_text(out, sizeof(out), &used, "%s%4uU", (file == 0U) ? "" : ", ", (unsigned int)map[rank][file]) != 0)
                {
                    return -1;
                }
            }
        }

        if (append_text(out, sizeof(out), &used, " }%s\n", (rank == (BOARD_RANK_COUNT - 1U)) ? "" : ",") != 0)
        {
            return -1;
        }
    }

    if (append_text(out, sizeof(out), &used, "};\n\n#endif\n") != 0)
    {
        return -1;
    }

    return write_file(path, out, used);
}

int main(int argc, char ** argv)
{
    char * text = NULL;
    size_t text_size = 0U;
    uint16_t map[BOARD_RANK_COUNT][BOARD_FILE_COUNT];
    uint32_t accepted;
    int rc = 0;

    if (argc != 2)
    {
        host_log(LOG_LEVEL_ERROR, "usage: parse_led_map <monitor-log>");
        return 2;
    }

    for (uint8_t rank = 0U; rank < BOARD_RANK_COUNT; rank++)
    {
        for (uint8_t file = 0U; file < BOARD_FILE_COUNT; file++)
        {
            map[rank][file] = LED_MAP_INVALID_INDEX;
        }
    }

    if (read_file(argv[1], &text, &text_size) != 0)
    {
        return 1;
    }

    (void)text_size;
    accepted = parse_accept_lines(text, map);
    free(text);

    if (accepted == 0U)
    {
        host_log(LOG_LEVEL_ERROR, "no MAP_ACCEPT lines found in %s", argv[1]);
        return 1;
    }

    if (write_json(map, "main/led_map.json") != 0)
    {
        rc = 1;
    }

    if (write_header(map, "main/led_map_generated.h") != 0)
    {
        rc = 1;
    }

    if (rc == 0)
    {
        host_log(LOG_LEVEL_INFO, "accepted squares: %u", (unsigned int)accepted);
        host_log(LOG_LEVEL_INFO, "saved JSON: main/led_map.json");
        host_log(LOG_LEVEL_INFO, "saved header: main/led_map_generated.h");
        host_log(LOG_LEVEL_INFO, "monitor log: %s", argv[1]);
    }

    return rc;
}
