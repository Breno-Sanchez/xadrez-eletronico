# Firmware

O firmware foi desenvolvido com **ESP-IDF** para o **ESP32 DevKit V1**.

## Estrutura dos arquivos principais

| Arquivo | Função |
|---|---|
| `main/main.c` | Inicialização e integração geral |
| `main/sensor.c` | Leitura da matriz de sensores |
| `main/sensor.h` | Interface do módulo de sensores |
| `main/led.c` | Controle de LEDs/indicadores |
| `main/led.h` | Interface do módulo de LED |
| `main/server.c` | Servidor ou interface de comunicação |
| `main/server.h` | Interface do módulo de servidor |
| `main/app_types.h` | Tipos compartilhados |

## Build

```bash
source ~/esp/esp-idf/export.sh
idf.py set-target esp32
idf.py build
```

## Flash

```bash
idf.py -p /dev/ttyACM0 flash monitor
```

## Organização futura recomendada

Se o projeto crescer, os módulos podem ser movidos para componentes ESP-IDF próprios:

```text
components/
├── board/
├── matrix_sensor/
├── web_server/
└── status_led/
```

Por enquanto, manter os arquivos em `main/` é adequado para o tamanho atual do protótipo.
