# Guia de montagem

## 1. Preparação da base

- Marcar as 64 casas do tabuleiro.
- Definir os barramentos de linhas e colunas.
- Planejar a saída dos fios para evitar cruzamentos.
- Reservar espaço para o ESP32 e para o roteamento dos jumpers.

## 2. Instalação dos reed switches

- Instalar um reed switch por casa.
- Posicionar o sensor próximo ao centro da casa.
- Conferir se o ímã da peça aciona o sensor com folga mecânica.
- Fixar os sensores apenas depois de testar acionamento.

## 3. Instalação dos diodos

Cada casa deve seguir a ligação:

```text
COLUNA ---- reed switch ---- anodo do diodo |>| catodo/faixa ---- LINHA
```

## 4. Ligação ao ESP32

- Conectar os barramentos aos GPIOs definidos em `main/main.c`.
- Manter GND comum.
- Usar resistores para estabilizar entradas quando necessário.
- Evitar fios soltos e contatos parcialmente presos.

## 5. Teste inicial

Antes de colocar todas as peças, testar uma casa por vez:

```text
a1, b1, c1, ..., h1
a2, b2, c2, ..., h2
...
a8, b8, c8, ..., h8
```
