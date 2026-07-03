#include "chess_logic.h"
#include <stdlib.h>
#include <string.h>

Peca tabuleiro[8][8];
CorPeca turno_atual = BRANCO;
char alvo_en_passant[3] = "-";
char fenAtual[128] = "";

extern void registrar_movimento_pgn(int lin_origem, int col_origem, int lin_destino, int col_destino, Peca origem, Peca destino, bool foi_ep);
extern bool rei_salvo(int lin_origem, int col_origem, int lin_destino, int col_destino);

void inicializar_tabuleiro() {
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            tabuleiro[i][j] = (Peca){VAZIO, NENHUMA, false, false};
        }
    }
    for (int j = 0; j < 8; j++) {
        tabuleiro[1][j] = (Peca){PEAO, PRETO, false, false};
        tabuleiro[6][j] = (Peca){PEAO, BRANCO, false, false};
    }
    tabuleiro[0][0] = tabuleiro[0][7] = (Peca){TORRE, PRETO, false, false};
    tabuleiro[7][0] = tabuleiro[7][7] = (Peca){TORRE, BRANCO, false, false};
    tabuleiro[0][1] = tabuleiro[0][6] = (Peca){CAVALO, PRETO, false, false};
    tabuleiro[7][1] = tabuleiro[7][6] = (Peca){CAVALO, BRANCO, false, false};
    tabuleiro[0][2] = tabuleiro[0][5] = (Peca){BISPO, PRETO, false, false};
    tabuleiro[7][2] = tabuleiro[7][5] = (Peca){BISPO, BRANCO, false, false};
    tabuleiro[0][3] = (Peca){RAINHA, PRETO, false, false};
    tabuleiro[0][4] = (Peca){REI, PRETO, false, false};
    tabuleiro[7][3] = (Peca){RAINHA, BRANCO, false, false};
    tabuleiro[7][4] = (Peca){REI, BRANCO, false, false};
}

void mover_peca(int lin_origem, int col_origem, int lin_destino, int col_destino) {
    if (validar_movimento(lin_origem, col_origem, lin_destino, col_destino)) {
        if (!rei_salvo(lin_origem, col_origem, lin_destino, col_destino)) return;
        
        Peca origem = tabuleiro[lin_origem][col_origem];
        Peca destino_alvo = tabuleiro[lin_destino][col_destino]; 
        char proximo_alvo_ep[3] = "-";
        bool foi_en_passant = false;

        if (origem.tipo == PEAO && abs(col_destino - col_origem) == 1 && tabuleiro[lin_destino][col_destino].tipo == VAZIO) {
            tabuleiro[lin_origem][col_destino] = (Peca){VAZIO, NENHUMA, false, false};
            foi_en_passant = true;
        }

        if (origem.tipo == PEAO && abs(lin_destino - lin_origem) == 2) {
            proximo_alvo_ep[0] = 'a' + col_origem;         
            proximo_alvo_ep[1] = '8' - ((lin_origem + lin_destino) / 2);         
            proximo_alvo_ep[2] = '\0';                     
            origem.dois = true;
        } else {
            origem.dois = false; 
        }

        if (origem.tipo == REI && abs(col_destino - col_origem) == 2) {
            if (col_destino > col_origem) { 
                tabuleiro[lin_origem][5] = tabuleiro[lin_origem][7]; 
                tabuleiro[lin_origem][7] = (Peca){VAZIO, NENHUMA, false, false}; 
                tabuleiro[lin_origem][5].ja_moveu = true;
            } else { 
                tabuleiro[lin_origem][3] = tabuleiro[lin_origem][0]; 
                tabuleiro[lin_origem][0] = (Peca){VAZIO, NENHUMA, false, false}; 
                tabuleiro[lin_origem][3].ja_moveu = true;
            }
        }

        tabuleiro[lin_destino][col_destino] = origem;
        tabuleiro[lin_destino][col_destino].ja_moveu = true;
        tabuleiro[lin_origem][col_origem] = (Peca){VAZIO, NENHUMA, false, false};

        // Promoção Automática p/ Rainha
        if (origem.tipo == PEAO && (lin_destino == 0 || lin_destino == 7)) {
            tabuleiro[lin_destino][col_destino].tipo = RAINHA;
        }

        registrar_movimento_pgn(lin_origem, col_origem, lin_destino, col_destino, origem, destino_alvo, foi_en_passant);
        strcpy(alvo_en_passant, proximo_alvo_ep);
        turno_atual = (turno_atual == BRANCO) ? PRETO : BRANCO;
        gerar_fen();
    }
}