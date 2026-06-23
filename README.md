# Xadrez Eletrônico

Projeto desenvolvido para a disciplina **OI25CP-7CPE**.

O projeto consiste em um tabuleiro de xadrez eletrônico baseado em uma matriz 8x8 de **reed switches**, **diodos**, **peças com ímãs de neodímio** e um microcontrolador **ESP32 DevKit V1**. O sistema detecta a presença das peças em cada casa do tabuleiro e permite visualizar o estado da matriz em tempo real.

---

## Objetivo

O objetivo do trabalho é desenvolver um protótipo funcional capaz de identificar automaticamente quais casas do tabuleiro estão ocupadas. A proposta integra eletrônica digital, sensores magnéticos, multiplexação de matriz, programação embarcada com ESP-IDF e organização de projeto técnico.

---

## Funcionalidades

- Detecção de presença de peças por **reed switch**.
- Matriz 8x8 com **diodos** para isolamento elétrico das casas.
- Peças de xadrez com **ímãs de neodímio** na base.
- Firmware embarcado para **ESP32 DevKit V1** usando **ESP-IDF**.
- Leitura multiplexada das linhas e colunas do tabuleiro.
- Exibição do estado do tabuleiro no terminal serial.
- Estrutura modular de código com sensores, LEDs e servidor.
- Documentação técnica com BOM, circuito, pinagem, montagem e testes.

---

## Funcionamento geral

Cada casa do tabuleiro possui um reed switch. Quando uma peça com ímã é posicionada sobre a casa, o campo magnético fecha o contato do reed switch. O ESP32 realiza a varredura da matriz e identifica a posição ocupada.

Ligação conceitual de uma casa:

```text
COLUNA ---- reed switch ---- anodo do diodo |>| catodo/faixa ---- LINHA
```

Fluxo de leitura:

```text
Peça com ímã
     ↓
Reed switch fecha
     ↓
ESP32 varre linha/coluna
     ↓
Firmware identifica a casa ocupada
     ↓
Terminal/servidor exibe o estado do tabuleiro
```

---

## Hardware

A lista de materiais está documentada em:

- [`docs/BOM.md`](docs/BOM.md)
- [`docs/BOM.csv`](docs/BOM.csv)

Principais componentes:

- ESP32 DevKit V1
- 64 reed switches
- 64 diodos de sinal
- Resistores de 10 kΩ
- 32 ímãs de neodímio
- Peças de xadrez
- Jumpers e fios para barramentos
- Base física do tabuleiro
- Cabo USB para alimentação, gravação e monitor serial

---

## Bill of Materials — BOM

| Item | Componente | Quantidade | Especificação | Observações |
|---:|---|---:|---|---|
| 1 | ESP32 DevKit V1 | 1 | Microcontrolador ESP32 | Controle da matriz, Wi-Fi e comunicação serial |
| 2 | Reed switch | 64 | Normalmente aberto | Um sensor por casa |
| 3 | Diodo de sinal | 64 | Ex.: 1N4148 | Isolamento elétrico da matriz |
| 4 | Resistor | 8 ou mais | 10 kΩ | Pull-up/pull-down conforme estratégia de leitura |
| 5 | Ímã de neodímio | 32 | Pequeno, para base da peça | Acionamento dos reed switches |
| 6 | Peças de xadrez | 32 | Conjunto padrão | Adaptadas com ímãs |
| 7 | Jumpers | Conforme montagem | Macho-fêmea/macho-macho | Ligação dos barramentos |
| 8 | Fios | Conforme montagem | Rígidos ou flexíveis | Linhas e colunas da matriz |
| 9 | Base do tabuleiro | 1 | MDF, acrílico, papelão ou impresso | Estrutura física |
| 10 | Cabo USB | 1 | USB para ESP32 | Alimentação e gravação |
| 11 | Fita/cola/fixação | Conforme montagem | Fita, cola quente ou similar | Organização mecânica |

---

## Circuito

Cada casa do tabuleiro é composta por um reed switch e um diodo.

```text
COLUNA ---- reed switch ---- anodo do diodo |>| catodo/faixa ---- LINHA
```

### Reed switch

O reed switch atua como uma chave magnética normalmente aberta. Quando o ímã da peça se aproxima, o contato fecha.

### Diodo

O diodo é usado para reduzir caminhos indesejados de corrente na matriz, principalmente quando várias peças estão posicionadas ao mesmo tempo.

### Resistores

Os resistores de 10 kΩ são usados para definir o estado lógico das entradas quando nenhum reed switch está fechado. Conforme a estratégia de varredura, podem atuar como pull-down ou pull-up.

### Cuidados elétricos

- Conferir a orientação dos diodos.
- Garantir GND comum entre ESP32 e matriz.
- Evitar fios soltos ou entradas flutuando.
- Testar continuidade de cada linha e coluna.
- Validar uma casa por vez antes de testar o jogo completo.
- Evitar GPIO1 e GPIO3, pois são usados pela UART principal do ESP32.
- Usar GPIO34 e GPIO35 com cuidado, pois são apenas entrada e não possuem pull-up/pull-down interno.

---

## Pinagem

A pinagem pode variar conforme a versão do firmware. A fonte definitiva é o arquivo [`main/main.c`](main/main.c).

Exemplo de mapeamento usado no protótipo:

### Colunas

| Coluna | GPIO |
|---|---:|
| A | GPIO23 |
| B | GPIO22 |
| C | GPIO21 |
| D | GPIO19 |
| E | GPIO18 |
| F | GPIO5 |
| G | GPIO17 |
| H | GPIO4 ou GPIO16 |

### Linhas

| Linha | GPIO |
|---:|---:|
| 1 | GPIO13 ou GPIO34 |
| 2 | GPIO14 ou GPIO35 |
| 3 | GPIO27 ou GPIO32 |
| 4 | GPIO26 ou GPIO33 |
| 5 | GPIO25 |
| 6 | GPIO33 ou GPIO26 |
| 7 | GPIO32 ou GPIO27 |
| 8 | GPIO4 ou GPIO14 |

---

## Estrutura do projeto

```text
.
├── CMakeLists.txt
├── README.md
├── LICENSE
├── partitions.csv
├── sdkconfig
├── sdkconfig.defaults
├── sdkconfig.ci
├── dependencies.lock
├── main
│   ├── CMakeLists.txt
│   ├── idf_component.yml
│   ├── app_types.h
│   ├── main.c
│   ├── led.c
│   ├── led.h
│   ├── sensor.c
│   ├── sensor.h
│   ├── server.c
│   └── server.h
├── docs
│   ├── BOM.csv
│   ├── BOM.md
│   ├── funcionamento.md
│   ├── montagem.md
│   ├── testes.md
│   ├── hardware
│   │   ├── circuito.md
│   │   └── pinout.md
│   └── assets
│       ├── images
│       └── diagrams
├── hardware
│   ├── stl
│   └── schematics
└── scripts
    ├── build.sh
    ├── flash_acm0.sh
    └── monitor_acm0.sh
```

---

## Como compilar

Entre na pasta do projeto:

```bash
cd /home/breno/Downloads/OI/Xadrez
```

Carregue o ESP-IDF:

```bash
source ~/esp/esp-idf/export.sh
```

Configure o alvo:

```bash
idf.py set-target esp32
```

Compile:

```bash
idf.py build
```

---

## Como gravar no ESP32

Para gravar usando a porta `/dev/ttyACM0`:

```bash
idf.py -p /dev/ttyACM0 flash monitor
```

Para sair do monitor serial:

```text
Ctrl + ]
```

---

## Scripts úteis

Também é possível usar os scripts da pasta `scripts/`:

```bash
./scripts/build.sh
./scripts/flash_acm0.sh
./scripts/monitor_acm0.sh
```

---

## Testes

### Teste sem peças

Resultado esperado:

```text
Pecas detectadas: 0
Casas ocupadas: nenhuma
```

### Teste com uma peça

Testar primeiro as casas extremas:

```text
a1
h1
a8
h8
```

A casa exibida deve ser exatamente a casa onde a peça foi colocada.

### Teste por fileira

Testar uma fileira por vez:

```text
a1 b1 c1 d1 e1 f1 g1 h1
a2 b2 c2 d2 e2 f2 g2 h2
...
a8 b8 c8 d8 e8 f8 g8 h8
```

### Teste do jogo inicial

Com o jogo em posição inicial, o esperado é:

```text
32 peças detectadas
linhas ocupadas: 1, 2, 7 e 8
```

### Diagnóstico rápido

| Sintoma | Possível causa |
|---|---|
| Casa acesa sem peça | Entrada flutuando, curto, reed preso ou resistor ausente |
| Linha inteira acesa | Linha flutuando ou curto no barramento |
| Coluna inteira ausente | GPIO errado, fio solto ou coluna sem continuidade |
| Casa invertida | Ordem de linhas/colunas diferente do array no firmware |
| Peça desaparece ao mover outra | Ghosting, diodo invertido, entrada instável ou caminho de fuga |
| Detecta poucas peças no jogo inicial | Reed não acionado, ímã fraco, distância excessiva ou mapeamento incorreto |

---

## Imagens

Fotos reais do protótipo devem ser adicionadas em:

```text
docs/assets/images/
```

Sugestões:

- `prototipo-tabuleiro.jpg`
- `esp32-ligado.jpg`
- `detalhe-reed-switches.jpg`
- `detalhe-diodos.jpg`
- `terminal-serial.jpg`
- `pecas-com-imas.jpg`

---

## Arquivos STL

Arquivos de impressão 3D devem ser adicionados em:

```text
hardware/stl/
```

Exemplos:

- suporte do ESP32
- base adaptada das peças
- suporte para ímã
- caixa de proteção
- espaçadores do tabuleiro

---

## Esquemáticos

Arquivos de circuito, diagramas elétricos e representações da matriz devem ser adicionados em:

```text
hardware/schematics/
```

Formatos recomendados:

- `.pdf`
- `.png`
- `.svg`
- KiCad
- EasyEDA

---

## Status

- [x] Montagem inicial da matriz
- [x] Integração com ESP32
- [x] Firmware base em ESP-IDF
- [x] Organização modular do código
- [ ] Validação individual das 64 casas
- [ ] Ajuste final contra leituras falsas
- [ ] Finalização dos arquivos STL
- [ ] Documentação final com fotos e esquemático

---

## Autor

**Breno Sanchez**  
GitHub: [@Breno-Sanchez](https://github.com/Breno-Sanchez)

---

## Licença

Este projeto está disponível sob a licença MIT. Consulte [`LICENSE`](LICENSE).
