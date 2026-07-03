#include "chess_logic.h"
#include <stdio.h>
#include <string.h>

void gerar_fen() {
    int pos = 0;
    int roque[4] = {1, 1, 1, 1}; // {K, Q, k, q}
    
    for (int l = 0; l < 8; l++) {
        int vazias = 0;
        for (int c = 0; c < 8; c++) {
            if (tabuleiro[l][c].tipo == VAZIO) {
                vazias++;
            } else {
                if (vazias > 0) {
                    pos += snprintf(&fenAtual[pos], sizeof(fenAtual) - pos, "%d", vazias);
                    vazias = 0;
                }
                char p_char = ' ';
                switch (tabuleiro[l][c].tipo) {
                    case PEAO:   p_char = 'p'; break;
                    case TORRE:  p_char = 'r'; break;
                    case CAVALO: p_char = 'n'; break;
                    case BISPO:  p_char = 'b'; break;
                    case RAINHA: p_char = 'q'; break;
                    case REI:    p_char = 'k'; break;
                    default: break;
                }
                if (tabuleiro[l][c].cor == BRANCO) p_char = p_char - 32;
                fenAtual[pos++] = p_char;
        
                if ((tabuleiro[l][c].tipo == REI) && (tabuleiro[l][c].ja_moveu == true)) {
                    if (tabuleiro[l][c].cor == BRANCO) { roque[0] = roque[1] = 0; } 
                    else { roque[2] = roque[3] = 0; }
                }
            }
        }
        if (vazias > 0) pos += snprintf(&fenAtual[pos], sizeof(fenAtual) - pos, "%d", vazias);
        if (l < 7) fenAtual[pos++] = '/';
    }

    if ((tabuleiro[0][0].tipo != TORRE) || (tabuleiro[0][0].ja_moveu == true)) roque[3] = 0;
    if ((tabuleiro[0][7].tipo != TORRE) || (tabuleiro[0][7].ja_moveu == true)) roque[2] = 0;
    if ((tabuleiro[7][0].tipo != TORRE) || (tabuleiro[7][0].ja_moveu == true)) roque[1] = 0;
    if ((tabuleiro[7][7].tipo != TORRE) || (tabuleiro[7][7].ja_moveu == true)) roque[0] = 0;
    
    pos += snprintf(&fenAtual[pos], sizeof(fenAtual) - pos, " %c ", (turno_atual == BRANCO) ? 'w' : 'b');

    char roque_str[5] = "";
    int r_idx = 0;
    if (roque[0] == 1) roque_str[r_idx++] = 'K';
    if (roque[1] == 1) roque_str[r_idx++] = 'Q';
    if (roque[2] == 1) roque_str[r_idx++] = 'k';
    if (roque[3] == 1) roque_str[r_idx++] = 'q';
    roque_str[r_idx] = '\0';
    if (r_idx == 0) strcpy(roque_str, "-");

    pos += snprintf(&fenAtual[pos], sizeof(fenAtual) - pos, "%s %s 0 1", roque_str, alvo_en_passant);
    fenAtual[pos] = '\0';
}