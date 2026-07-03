#ifndef CHESS_LOGIC_H
#define CHESS_LOGIC_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { VAZIO = 0, PEAO, TORRE, CAVALO, BISPO, RAINHA, REI } TipoPeca;
typedef enum { NENHUMA = 0, BRANCO, PRETO } CorPeca;

typedef struct {
    TipoPeca tipo;
    CorPeca cor;
    bool ja_moveu; 
    bool dois;     
} Peca;

extern Peca tabuleiro[8][8];
extern CorPeca turno_atual;
extern char alvo_en_passant[3];
extern char fenAtual[128];

void inicializar_tabuleiro(void);
void inicializar_pgn(void);
bool validar_movimento(int lin_origem, int col_origem, int lin_destino, int col_destino);
void mover_peca(int lin_origem, int col_origem, int lin_destino, int col_destino);
void gerar_fen(void);

#ifdef __cplusplus
}
#endif

#endif // CHESS_LOGIC_H