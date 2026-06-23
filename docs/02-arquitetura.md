# Arquitetura

A arquitetura do projeto é dividida em três camadas principais:

1. Hardware da matriz.
2. Firmware embarcado.
3. Interface de visualização/processamento.

## Arquitetura física

```text
Peças com ímãs
      ↓
Matriz 8x8 de reed switches + diodos
      ↓
Barramentos de linhas e colunas
      ↓
ESP32 DevKit V1
      ↓
Terminal serial / servidor / interface
```

## Arquitetura de firmware

```text
main.c
  ├── inicialização geral
  ├── tarefas principais
  └── integração dos módulos

sensor.c / sensor.h
  ├── leitura da matriz
  ├── varredura linha/coluna
  └── estabilização das leituras

led.c / led.h
  ├── indicação visual
  └── controle de LEDs

server.c / server.h
  ├── servidor/interface
  └── exposição do estado do tabuleiro

app_types.h
  └── tipos compartilhados do projeto
```

## Decisões técnicas

- Uso de ESP32 DevKit V1 pela disponibilidade e suporte ao ESP-IDF.
- Uso de reed switches para detecção magnética simples e robusta.
- Uso de diodos para reduzir caminhos indesejados de corrente na matriz.
- Separação modular do firmware para facilitar manutenção.
