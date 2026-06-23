# Testes

## Teste sem peças

Resultado esperado:

```text
Pecas detectadas: 0
Casas ocupadas: nenhuma
```

## Teste com uma peça

Testar primeiro as casas extremas:

```text
a1
h1
a8
h8
```

A casa exibida deve ser exatamente a casa onde a peça foi colocada.

## Teste por fileira

Testar uma fileira por vez:

```text
a1 b1 c1 d1 e1 f1 g1 h1
a2 b2 c2 d2 e2 f2 g2 h2
...
a8 b8 c8 d8 e8 f8 g8 h8
```

## Teste do jogo inicial

Com o jogo em posição inicial, o esperado é:

```text
32 peças detectadas
linhas ocupadas: 1, 2, 7 e 8
```

## Diagnóstico rápido

| Sintoma | Possível causa |
|---|---|
| Casa acesa sem peça | Entrada flutuando, curto, reed preso ou resistor ausente |
| Linha inteira acesa | Linha flutuando ou curto no barramento |
| Coluna inteira ausente | GPIO errado, fio solto ou coluna sem continuidade |
| Casa invertida | Ordem de linhas/colunas diferente do array no firmware |
| Peça desaparece ao mover outra | Ghosting, diodo invertido, entrada instável ou caminho de fuga |
| Detecta poucas peças no jogo inicial | Reed não acionado, ímã fraco, distância excessiva ou mapeamento incorreto |
