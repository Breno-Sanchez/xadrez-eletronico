#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

#define ROWS 8
#define COLS 8

#define DISCHARGE_DELAY_MS 5
#define SETTLE_DELAY_MS    5
#define SAMPLE_COUNT       3
#define SAMPLE_DELAY_MS    1
#define REQUIRED_HIGH      2

static const gpio_num_t col_pins[COLS] = {
    GPIO_NUM_23, // a
    GPIO_NUM_22, // b
    GPIO_NUM_21, // c
    GPIO_NUM_19, // d
    GPIO_NUM_18, // e
    GPIO_NUM_5,  // f
    GPIO_NUM_17, // g
    GPIO_NUM_4   // h
};

static const gpio_num_t row_pins[ROWS] = {
    GPIO_NUM_34, // 1
    GPIO_NUM_35, // 2
    GPIO_NUM_32, // 3
    GPIO_NUM_33, // 4
    GPIO_NUM_25, // 5
    GPIO_NUM_26, // 6
    GPIO_NUM_27, // 7
    GPIO_NUM_14  // 8
};

static const char col_names[COLS] = {'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h'};

static void configure_gpio(void)
{
    uint64_t col_mask = 0;
    uint64_t row_mask = 0;

    for (int i = 0; i < COLS; i++) {
        col_mask |= (1ULL << col_pins[i]);
    }

    for (int i = 0; i < ROWS; i++) {
        row_mask |= (1ULL << row_pins[i]);
    }

    gpio_config_t col_cfg = {
        .pin_bit_mask = col_mask,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    gpio_config_t row_cfg = {
        .pin_bit_mask = row_mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    gpio_config(&col_cfg);
    gpio_config(&row_cfg);

    for (int c = 0; c < COLS; c++) {
        gpio_set_level(col_pins[c], 0);
    }
}

static void scan_matrix(bool board[ROWS][COLS])
{
    memset(board, 0, sizeof(bool) * ROWS * COLS);

    for (int col = 0; col < COLS; col++) {
        for (int c = 0; c < COLS; c++) {
            gpio_set_level(col_pins[c], 0);
        }

        vTaskDelay(pdMS_TO_TICKS(DISCHARGE_DELAY_MS));

        gpio_set_level(col_pins[col], 1);

        vTaskDelay(pdMS_TO_TICKS(SETTLE_DELAY_MS));

        for (int row = 0; row < ROWS; row++) {
            int high_count = 0;

            for (int s = 0; s < SAMPLE_COUNT; s++) {
                high_count += gpio_get_level(row_pins[row]);
                vTaskDelay(pdMS_TO_TICKS(SAMPLE_DELAY_MS));
            }

            board[row][col] = (high_count >= REQUIRED_HIGH);
        }

        gpio_set_level(col_pins[col], 0);
    }
}

static bool boards_are_equal(bool a[ROWS][COLS], bool b[ROWS][COLS])
{
    return memcmp(a, b, sizeof(bool) * ROWS * COLS) == 0;
}

static int count_pieces(bool board[ROWS][COLS])
{
    int count = 0;

    for (int row = 0; row < ROWS; row++) {
        for (int col = 0; col < COLS; col++) {
            if (board[row][col]) {
                count++;
            }
        }
    }

    return count;
}

static void print_board(bool board[ROWS][COLS])
{
    int pieces = count_pieces(board);

    printf("\nMatriz de reeds detectada\n");
    printf("Pecas detectadas: %d\n\n", pieces);

    printf("      a b c d e f g h\n");
    printf("    +-----------------+\n");

    for (int rank = 8; rank >= 1; rank--) {
        int row = rank - 1;

        printf(" %d  |", rank);

        for (int col = 0; col < COLS; col++) {
            printf(" %c", board[row][col] ? 'X' : '.');
        }

        printf(" |  %d\n", rank);
    }

    printf("    +-----------------+\n");
    printf("      a b c d e f g h\n\n");

    printf("Casas ocupadas:");

    if (pieces == 0) {
        printf(" nenhuma");
    } else {
        for (int row = 0; row < ROWS; row++) {
            for (int col = 0; col < COLS; col++) {
                if (board[row][col]) {
                    printf(" %c%d", col_names[col], row + 1);
                }
            }
        }
    }

    printf("\n--------------------------------------------------\n");
}

void app_main(void)
{
    bool board[ROWS][COLS];
    bool last_board[ROWS][COLS];

    memset(board, 0, sizeof(board));
    memset(last_board, 0xFF, sizeof(last_board));

    configure_gpio();

    printf("\nTeste simples da matriz de reed switches\n");
    printf("Colunas: saidas GPIO\n");
    printf("Linhas: entradas GPIO com pull-down externo de 10k para GND\n");
    printf("Sem pecas, o esperado e: Pecas detectadas: 0\n");

    while (1) {
        scan_matrix(board);

        if (!boards_are_equal(board, last_board)) {
            print_board(board);
            memcpy(last_board, board, sizeof(last_board));
        }

        vTaskDelay(pdMS_TO_TICKS(300));
    }
}
