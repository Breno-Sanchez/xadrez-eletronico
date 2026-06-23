# Hardware

## Matriz de sensores

O tabuleiro usa uma matriz 8x8. Cada interseção representa uma casa do xadrez.

Cada casa possui:

- 1 reed switch;
- 1 diodo de sinal;
- conexão com uma linha;
- conexão com uma coluna.

## Ligação conceitual

```text
COLUNA ---- reed switch ---- anodo do diodo |>| catodo/faixa ---- LINHA
```

## Reed switch

O reed switch é uma chave magnética normalmente aberta. Quando a peça com ímã se aproxima, o contato fecha.

## Diodo

O diodo reduz leituras falsas causadas por caminhos alternativos na matriz, especialmente quando várias peças estão posicionadas simultaneamente.

## Resistores

Os resistores de 10 kΩ são usados para estabilizar o nível lógico das entradas. Dependendo da estratégia de varredura, podem ser usados como pull-down ou pull-up.

## Cuidados com GPIOs do ESP32

- Evitar GPIO1 e GPIO3, pois são usados pela UART principal.
- GPIO34 e GPIO35 são apenas entrada.
- GPIO34 e GPIO35 não possuem pull-up/pull-down interno.
- GPIO0, GPIO2, GPIO12 e GPIO15 devem ser usados com cuidado por influência no boot.
- A fonte definitiva de pinagem é o arquivo `main/main.c`.

## Validação elétrica recomendada

Antes de testar o firmware completo:

1. Conferir continuidade de cada coluna.
2. Conferir continuidade de cada linha.
3. Conferir orientação dos diodos.
4. Testar cada reed switch com multímetro.
5. Testar uma peça por vez no firmware.
