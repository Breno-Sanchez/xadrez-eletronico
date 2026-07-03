#include "chess_logic.h" // Puxa as structs, variáveis globais e enums daqui!
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Variáveis Globais exclusivas para a anotação da partida (PGN)
char pgn_atual[2048]; 
int pgn_pos = 0;      
int numero_rodada = 1;

// Declarações prévias locais
void registrar_movimento_pgn(int lin_origem, int col_origem, int lin_destino, int col_destino, Peca origem, Peca destino, bool foi_ep);
bool xeque(void);

void inicializar_pgn() {
    pgn_pos = 0;
    pgn_atual[0] = '\0';
    numero_rodada = 1;
    
    pgn_pos += snprintf(&pgn_atual[pgn_pos], sizeof(pgn_atual) - pgn_pos,
        "[Event \"Partida ESP32\"]\n"
        "[Site \"Xadrez Embarcado\"]\n"
        "[Date \"2026.01.01\"]\n" 
        "[Round \"1\"]\n"
        "[White \"Brancas\"]\n"
        "[Black \"Pretas\"]\n"
        "[Result \"*\"]\n\n");
}

bool validar_movimento(int lin_origem, int col_origem, int lin_destino, int col_destino) {
    Peca origem = tabuleiro[lin_origem][col_origem];
    Peca destino = tabuleiro[lin_destino][col_destino];

    if (origem.tipo == VAZIO) return false;
    if (origem.cor == destino.cor) return false;

    int diff_lin = abs(lin_destino - lin_origem);
    int diff_col = abs(col_destino - col_origem);
    
    // Vetores de direção (Passos): Resultam em -1, 0 ou 1
    int passo_lin = (lin_destino > lin_origem) ? 1 : ((lin_destino < lin_origem) ? -1 : 0);
    int passo_col = (col_destino > col_origem) ? 1 : ((col_destino < col_origem) ? -1 : 0);

    switch (origem.tipo) {
        case CAVALO:
            if ((diff_lin == 2 && diff_col == 1) || (diff_lin == 1 && diff_col == 2)) return true;
            return false;

        case BISPO:
            if (diff_lin == diff_col) {
                for (int i = 1; i < diff_lin; i++) {
                    if (tabuleiro[lin_origem + (i * passo_lin)][col_origem + (i * passo_col)].tipo != VAZIO) {
                        return false;
                    }
                }
                return true;
            }
            return false;

        case TORRE:
            if ((diff_lin >= 1 && diff_col == 0) || (diff_lin == 0 && diff_col >= 1)) {
                int dist = (diff_lin > 0) ? diff_lin : diff_col;
                for (int i = 1; i < dist; i++) {
                    if (tabuleiro[lin_origem + (i * passo_lin)][col_origem + (i * passo_col)].tipo != VAZIO) {
                        return false;
                    }
                }
                return true;
            }
            return false;

        case RAINHA:
            if ((diff_lin == diff_col) || (diff_lin >= 1 && diff_col == 0) || (diff_lin == 0 && diff_col >= 1)) {
                int dist = (diff_lin > diff_col) ? diff_lin : diff_col; 
                for (int i = 1; i < dist; i++) {
                    if (tabuleiro[lin_origem + (i * passo_lin)][col_origem + (i * passo_col)].tipo != VAZIO) {
                        return false;
                    }
                }
                return true;
            }
            return false;

        case REI:
            if (diff_lin <= 1 && diff_col <= 1) {
                if (casa_atacada(lin_destino, col_destino, (origem.cor == BRANCO) ? PRETO : BRANCO) == false) {
                    return true;
                }
            }
            // Roque
            if (diff_lin == 0 && diff_col == 2 && !origem.ja_moveu) {
                if (casa_atacada(lin_origem, col_origem, (origem.cor == BRANCO) ? PRETO : BRANCO)) return false;
                
                if (passo_col == -1) {  // Roque maior
                    if (tabuleiro[lin_origem][0].tipo == TORRE && !tabuleiro[lin_origem][0].ja_moveu) {
                        if (tabuleiro[lin_origem][1].tipo == VAZIO && tabuleiro[lin_origem][2].tipo == VAZIO && tabuleiro[lin_origem][3].tipo == VAZIO) {
                            if (!casa_atacada(lin_origem, 2, (origem.cor == BRANCO) ? PRETO : BRANCO) &&
                                !casa_atacada(lin_origem, 3, (origem.cor == BRANCO) ? PRETO : BRANCO)) {
                                return true;
                            }
                        }
                    }
                } 
                else if (passo_col == 1) { // Roque menor
                    if (tabuleiro[lin_origem][7].tipo == TORRE && !tabuleiro[lin_origem][7].ja_moveu) {
                         if (tabuleiro[lin_origem][5].tipo == VAZIO && tabuleiro[lin_origem][6].tipo == VAZIO) {
                             if (!casa_atacada(lin_origem, 5, (origem.cor == BRANCO) ? PRETO : BRANCO) &&
                                 !casa_atacada(lin_origem, 6, (origem.cor == BRANCO) ? PRETO : BRANCO)) {
                                 return true;
                             }
                         }
                    }
                }
            }
            return false;

        case PEAO: {
            int dir = (origem.cor == BRANCO) ? -1 : 1; 
            int mov_lin = lin_destino - lin_origem; 
            
            if (mov_lin == dir && diff_col == 0 && destino.tipo == VAZIO) {
                return true;
            }
            if (!origem.ja_moveu && mov_lin == (2 * dir) && diff_col == 0 && destino.tipo == VAZIO) {
                if (tabuleiro[lin_origem + dir][col_origem].tipo == VAZIO) {
                    return true;
                }
            }
            if (mov_lin == dir && diff_col == 1 && destino.tipo != VAZIO) {
                return true;
            }
            // En Passant
            if (mov_lin == dir && diff_col == 1 && destino.tipo == VAZIO) {
                Peca vizinho = tabuleiro[lin_origem][col_destino];
                if (vizinho.tipo == PEAO && vizinho.cor != origem.cor && vizinho.dois) {
                    return true;
                }
            }
            return false;
        }

        default:
            return false;
    }
}

bool casa_atacada(int lin_alvo, int col_alvo, CorPeca cor_atacante) {
    for (int l = 0; l < 8; l++) {
        for (int c = 0; c < 8; c++) {
            if (tabuleiro[l][c].cor == cor_atacante) {
                if (tabuleiro[l][c].tipo == REI) {
                    if (abs(lin_alvo - l) <= 1 && abs(col_alvo - c) <= 1) {
                        return true;
                    }
                }
                else if (tabuleiro[l][c].tipo == PEAO) {
                    int dir = (cor_atacante == BRANCO) ? -1 : 1;
                    if ((lin_alvo - l) == dir && abs(col_alvo - c) == 1) {
                        return true;
                    }
                }
                else {
                    if (validar_movimento(l, c, lin_alvo, col_alvo)) {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

bool xeque() {
    char cor_do_rei = (turno_atual == BRANCO) ? BRANCO : PRETO;
    char cor_do_atacante = (turno_atual == BRANCO) ? PRETO : BRANCO;
    
    for (int l = 0; l < 8; ++l) {
        for (int c = 0; c < 8; ++c) {
            if (tabuleiro[l][c].tipo == REI && tabuleiro[l][c].cor == cor_do_rei) {
                return casa_atacada(l, c, cor_do_atacante);
            }
        }
    }
    return false;
}

bool rei_salvo(int lin_origem, int col_origem, int lin_destino, int col_destino) {
    Peca peca_origem = tabuleiro[lin_origem][col_origem];
    Peca peca_destino = tabuleiro[lin_destino][col_destino];

    tabuleiro[lin_destino][col_destino] = peca_origem;
    tabuleiro[lin_origem][col_origem] = (Peca){VAZIO, NENHUMA, false, false};

    bool seguro = !xeque(); 

    tabuleiro[lin_origem][col_origem] = peca_origem;
    tabuleiro[lin_destino][col_destino] = peca_destino;

    return seguro;
}

void registrar_movimento_pgn(int lin_origem, int col_origem, int lin_destino, int col_destino, Peca origem, Peca destino, bool foi_ep) {
    char lance_str[16] = "";
    int idx = 0;

    if (origem.cor == BRANCO) {
        idx += snprintf(&lance_str[idx], sizeof(lance_str) - idx, "%d. ", numero_rodada);
    }

    if (origem.tipo == REI && abs(col_destino - col_origem) == 2) {
        if (col_destino > col_origem) {
            strcpy(&lance_str[idx], "O-O");
        } else {
            strcpy(&lance_str[idx], "O-O-O");
        }
        idx = strlen(lance_str);
    } 
    else {
        if (origem.tipo == REI)    lance_str[idx++] = 'K';
        if (origem.tipo == RAINHA) lance_str[idx++] = 'Q';
        if (origem.tipo == TORRE)  lance_str[idx++] = 'R';
        if (origem.tipo == BISPO)  lance_str[idx++] = 'B';
        if (origem.tipo == CAVALO) lance_str[idx++] = 'N';

        if (origem.tipo == PEAO && (destino.tipo != VAZIO || foi_ep)) {
            lance_str[idx++] = 'a' + col_origem;
        }

        if (destino.tipo != VAZIO || foi_ep) {
            lance_str[idx++] = 'x';
        }

        lance_str[idx++] = 'a' + col_destino;
        lance_str[idx++] = '8' - lin_destino;
        lance_str[idx] = '\0';
    }

    CorPeca cor_inimiga = (origem.cor == BRANCO) ? PRETO : BRANCO;
    int rei_lin = -1, rei_col = -1;
    
    for (int l = 0; l < 8; l++) {
        for (int c = 0; c < 8; c++) {
            if (tabuleiro[l][c].tipo == REI && tabuleiro[l][c].cor == cor_inimiga) {
                rei_lin = l;
                rei_col = c;
                break;
            }
        }
        if (rei_lin != -1) break;
    }
    
    if (rei_lin != -1 && casa_atacada(rei_lin, rei_col, origem.cor)) {
        strcat(lance_str, "+");
    }

    pgn_pos += snprintf(&pgn_atual[pgn_pos], sizeof(pgn_atual) - pgn_pos, "%s ", lance_str);
    if (origem.cor == PRETO) {
        numero_rodada++;