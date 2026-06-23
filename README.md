# Xadrez Eletrônico

Projeto desenvolvido para a disciplina **OI25CP-7CPE**.

O **Xadrez Eletrônico** é um protótipo de tabuleiro inteligente baseado em **ESP32**, matriz 8x8 de **reed switches**, **diodos** e peças de xadrez com **ímãs de neodímio**. O sistema detecta automaticamente quais casas estão ocupadas e disponibiliza o estado do tabuleiro via firmware embarcado.

---

## Visão geral

Cada casa do tabuleiro possui um reed switch. Quando uma peça com ímã é posicionada sobre a casa, o sensor fecha contato. O ESP32 realiza a varredura da matriz de linhas e colunas e identifica a posição ocupada.

```text
Peça com ímã
     ↓
Reed switch fecha
     ↓
ESP32 varre a matriz 8x8
     ↓
Firmware identifica a casa ocupada
     ↓
Estado do tabuleiro é exibido/processado
```

---

## Funcionalidades

- Detecção de presença de peças por reed switch.
- Matriz 8x8 com isolamento por diodos.
- Peças de xadrez adaptadas com ímãs de neodímio.
- Firmware embarcado em ESP-IDF para ESP32 DevKit V1.
- Estrutura modular para sensores, LEDs e servidor.
- Scripts auxiliares para build, flash e monitor serial.
- Documentação técnica com BOM, circuito, pinagem, montagem e testes.

---

## Hardware principal

| Componente | Quantidade | Função |
|---|---:|---|
| ESP32 DevKit V1 | 1 | Controle da matriz e comunicação |
| Reed switches | 64 | Detecção magnética das casas |
| Diodos de sinal | 64 | Isolamento elétrico da matriz |
| Ímãs de neodímio | 32 | Acionamento dos sensores |
| Peças de xadrez | 32 | Peças adaptadas com ímãs |
| Resistores de 10 kΩ | 8 ou mais | Estabilização lógica das entradas |

A lista completa está em [`docs/BOM.md`](docs/BOM.md).

---

## Documentação

| Documento | Conteúdo |
|---|---|
| [`docs/01-visao-geral.md`](docs/01-visao-geral.md) | Objetivo, escopo e funcionamento geral |
| [`docs/02-arquitetura.md`](docs/02-arquitetura.md) | Arquitetura de hardware e firmware |
| [`docs/03-hardware.md`](docs/03-hardware.md) | Circuito, matriz, diodos, resistores e pinagem |
| [`docs/04-firmware.md`](docs/04-firmware.md) | Organização do firmware ESP-IDF |
| [`docs/05-montagem.md`](docs/05-montagem.md) | Guia de montagem física e elétrica |
| [`docs/06-testes.md`](docs/06-testes.md) | Roteiro de validação e diagnóstico |
| [`docs/BOM.md`](docs/BOM.md) | Bill of Materials em Markdown |
| [`docs/BOM.csv`](docs/BOM.csv) | Bill of Materials em CSV |

---

## Estrutura do projeto

```text
.
├── CMakeLists.txt
├── README.md
├── LICENSE
├── partitions.csv
├── sdkconfig.defaults
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
│   ├── 01-visao-geral.md
│   ├── 02-arquitetura.md
│   ├── 03-hardware.md
│   ├── 04-firmware.md
│   ├── 05-montagem.md
│   ├── 06-testes.md
│   ├── BOM.md
│   ├── BOM.csv
│   └── assets
│       ├── images
│       └── diagrams
├── hardware
│   ├── schematics
│   └── stl
└── scripts
    ├── build.sh
    ├── flash_acm0.sh
    ├── monitor_acm0.sh
    └── clean.sh
```

---

## Como compilar

```bash
cd /home/breno/Downloads/OI/Xadrez
source ~/esp/esp-idf/export.sh
idf.py set-target esp32
idf.py build
```

Ou usando script:

```bash
./scripts/build.sh
```

---

## Como gravar no ESP32

Para gravar pela porta `/dev/ttyACM0`:

```bash
idf.py -p /dev/ttyACM0 flash monitor
```

Ou usando script:

```bash
./scripts/flash_acm0.sh
./scripts/monitor_acm0.sh
```

Para sair do monitor serial:

```text
Ctrl + ]
```

---

## Status do projeto

- [x] Montagem inicial da matriz 8x8.
- [x] Integração inicial com ESP32.
- [x] Firmware base em ESP-IDF.
- [x] Organização modular do código.
- [ ] Validação individual das 64 casas.
- [ ] Ajuste final contra leituras falsas.
- [ ] Documentação final com fotos do protótipo.
- [ ] Adição dos esquemáticos finais.
- [ ] Adição dos arquivos STL finais.

---

## Autor

**Breno Sanchez**  
GitHub: [@Breno-Sanchez](https://github.com/Breno-Sanchez)

---

## Licença

Este projeto está disponível sob a licença MIT. Consulte [`LICENSE`](LICENSE).
