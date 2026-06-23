#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "esp_rom_sys.h"

#define N 8

#define SETTLE_US        1800
#define SAMPLE_GAP_US    150
#define STABLE_READS     7
#define STABLE_MIN_HIGH  5
#define LOOP_DELAY_MS    80
#define CONFIRM_SCANS    3

/*
    Matriz 8x8 reed switch + diodo - ESP32 DevKit V1

    Ligação por casa:

    COLUNA ---- reed ---- anodo do diodo |>| catodo/faixa ---- LINHA

    Modo de varredura:
    - colunas A-H são saídas apenas quando selecionadas;
    - coluna selecionada = OUTPUT HIGH;
    - colunas não selecionadas = INPUT/FLOATING, alta impedância;
    - linhas 1-8 são entradas;
    - reed fechado leva HIGH da coluna para a linha.

    Este código assume o mapeamento físico atual da montagem:
    h no GPIO4, linhas 1 e 2 nos GPIO34/GPIO35.
*/

// Colunas A-H, barramento inferior.
static const gpio_num_t COL_PINS[N] = {
    GPIO_NUM_23,  // a
    GPIO_NUM_22,  // b
    GPIO_NUM_21,  // c
    GPIO_NUM_19,  // d
    GPIO_NUM_18,  // e
    GPIO_NUM_5,   // f
    GPIO_NUM_17,  // g
    GPIO_NUM_4    // h
};

// Linhas 1-8, barramento lateral.
static const gpio_num_t ROW_PINS[N] = {
    GPIO_NUM_34,  // 1
    GPIO_NUM_35,  // 2
    GPIO_NUM_32,  // 3
    GPIO_NUM_33,  // 4
    GPIO_NUM_25,  // 5
    GPIO_NUM_26,  // 6
    GPIO_NUM_27,  // 7
    GPIO_NUM_14   // 8
};

static bool displayed_board[N][N];
static bool candidate_board[N][N];
static int candidate_count = 0;
static bool has_displayed_once = false;

static bool pin_has_internal_pulldown(gpio_num_t pin)
{
    return !(pin == GPIO_NUM_34 ||
             pin == GPIO_NUM_35 ||
             pin == GPIO_NUM_36 ||
             pin == GPIO_NUM_39);
}

static void all_columns_hiz(void)
{
    for (int c = 0; c < N; c++) {
        gpio_set_level(COL_PINS[c], 0);
        gpio_set_direction(COL_PINS[c], GPIO_MODE_INPUT);
        gpio_pullup_dis(COL_PINS[c]);
        gpio_pulldown_dis(COL_PINS[c]);
    }
}

static void drive_column_high(int c)
{
    gpio_set_level(COL_PINS[c], 1);
    gpio_set_direction(COL_PINS[c], GPIO_MODE_OUTPUT);
    gpio_set_level(COL_PINS[c], 1);
}

static void gpio_init_matrix(void)
{
    // Colunas começam em alta impedância.
    for (int c = 0; c < N; c++) {
        gpio_reset_pin(COL_PINS[c]);
        gpio_set_direction(COL_PINS[c], GPIO_MODE_INPUT);
        gpio_pullup_dis(COL_PINS[c]);
        gpio_pulldown_dis(COL_PINS[c]);
    }

    // Linhas como entrada.
    for (int r = 0; r < N; r++) {
        gpio_reset_pin(ROW_PINS[r]);
        gpio_set_direction(ROW_PINS[r], GPIO_MODE_INPUT);
        gpio_pullup_dis(ROW_PINS[r]);

        if (pin_has_internal_pulldown(ROW_PINS[r])) {
            gpio_pulldown_en(ROW_PINS[r]);
        } else {
            gpio_pulldown_dis(ROW_PINS[r]);
        }
    }

    all_columns_hiz();
}

static bool read_row_stable(gpio_num_t pin)
{
    int high_count = 0;

    for (int i = 0; i < STABLE_READS; i++) {
        if (gpio_get_level(pin)) {
            high_count++;
        }

        esp_rom_delay_us(SAMPLE_GAP_US);
    }

    return high_count >= STABLE_MIN_HIGH;
}

static void scan_matrix(bool out[N][N])
{
    memset(out, 0, sizeof(bool) * N * N);

    for (int c = 0; c < N; c++) {
        all_columns_hiz();
        esp_rom_delay_us(SETTLE_US);

        drive_column_high(c);
        esp_rom_delay_us(SETTLE_US);

        for (int r = 0; r < N; r++) {
            out[r][c] = read_row_stable(ROW_PINS[r]);
        }

        all_columns_hiz();
        esp_rom_delay_us(SETTLE_US);
    }

    all_columns_hiz();
}

static int count_pieces(const bool b[N][N])
{
    int count = 0;

    for (int r = 0; r < N; r++) {
        for (int c = 0; c < N; c++) {
            if (b[r][c]) {
                count++;
            }
        }
    }

    return count;
}

static void print_coordinates(const bool b[N][N])
{
    printf("Casas ocupadas: ");

    bool first = true;

    for (int r = 0; r < N; r++) {
        for (int c = 0; c < N; c++) {
            if (b[r][c]) {
                if (!first) {
                    printf(", ");
                }

                printf("%c%d", 'a' + c, r + 1);
                first = false;
            }
        }
    }

    if (first) {
        printf("nenhuma");
    }

    printf("\n");
}

static void print_board(const bool b[N][N])
{
    printf("\033[2J\033[H");

    printf("Teste matriz 8x8 reed switch + diodo - ESP32 DevKit V1\n");
    printf("Modo: coluna ativa OUTPUT HIGH, colunas inativas HI-Z, linhas INPUT\n");
    printf("Diodo: anodo na coluna, catodo/faixa na linha\n");
    printf("Atualizacao: somente depois de %d leituras iguais\n\n", CONFIRM_SCANS);

    printf("Pecas detectadas: %d\n", count_pieces(b));
    print_coordinates(b);
    printf("\n");

    printf("        a   b   c   d   e   f   g   h\n");
    printf("      +---+---+---+---+---+---+---+---+\n");

    for (int r = N - 1; r >= 0; r--) {
        printf("  %d   |", r + 1);

        for (int c = 0; c < N; c++) {
            printf(" %c |", b[r][c] ? 'X' : ' ');
        }

        printf("   %d\n", r + 1);
        printf("      +---+---+---+---+---+---+---+---+\n");
    }

    printf("        a   b   c   d   e   f   g   h\n\n");

    printf("COLUNAS A-H, barramento inferior:\n");
    printf("a=GPIO23  b=GPIO22  c=GPIO21  d=GPIO19  e=GPIO18  f=GPIO5  g=GPIO17  h=GPIO4\n\n");

    printf("LINHAS 1-8, barramento lateral:\n");
    printf("1=GPIO34  2=GPIO35  3=GPIO32  4=GPIO33  5=GPIO25  6=GPIO26  7=GPIO27  8=GPIO14\n\n");

    printf("Observacao: GPIO34/GPIO35 nao possuem pulldown interno.\n");
    printf("Se linha 1 ou 2 ficar instavel, coloque 10 kOhm dessas linhas para GND.\n");
    printf("Com jogo inicial completo, esperado: 32 pecas.\n");

    fflush(stdout);
}

void app_main(void)
{
    gpio_init_matrix();

    memset(displayed_board, 0, sizeof(displayed_board));
    memset(candidate_board, 0, sizeof(candidate_board));

    while (true) {
        bool now[N][N];

        scan_matrix(now);

        if (memcmp(now, candidate_board, sizeof(now)) == 0) {
            if (candidate_count < CONFIRM_SCANS) {
                candidate_count++;
            }
        } else {
            memcpy(candidate_board, now, sizeof(now));
            candidate_count = 1;
        }

        if (candidate_count >= CONFIRM_SCANS) {
            bool changed = !has_displayed_once ||
                           memcmp(candidate_board, displayed_board, sizeof(candidate_board)) != 0;

            if (changed) {
                memcpy(displayed_board, candidate_board, sizeof(candidate_board));
                has_displayed_once = true;
                print_board(displayed_board);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(LOOP_DELAY_MS));
    }
}
