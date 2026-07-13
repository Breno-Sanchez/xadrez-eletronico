# ESP32-S3 Electronic Chessboard — Technical Report

**Project:** Electronic Chessboard  
**Repository branch:** `Breno`  
**Target platform:** ESP32-S3 with ESP-IDF v5.3.x  
**Main objective:** combine a physical chessboard, reed-switch sensing, LED feedback, local rule validation, web monitoring, and optional StockfishOnline move advice in one embedded system.

---

## Demonstration video

The following video shows the physical board, LED feedback, and web interface during gameplay.

https://github.com/user-attachments/assets/bd02f91b-ea6f-4f28-a71c-ffd5dfbfd848

Local repository fallback, when the MP4 is committed under `docs/media/xadrez.mp4`:

<video src="docs/media/xadrez.mp4" width="720" controls></video>

---

## 1. Overview

The project implements an ESP32-S3 electronic chessboard that detects piece presence using an 8×8 reed-switch matrix. The firmware maintains a virtual chess game, validates moves locally, renders feedback on an addressable LED strip, and exposes a local web interface through the ESP32 SoftAP.

The system is designed for interactive gameplay and teaching. It allows players to see legal moves, invalid moves, check/checkmate states, draw state, chess clocks, FEN/PGN notation, captured material, and optional StockfishOnline best-move hints.

---

## 2. Main features

### Physical board sensing

- 8×8 reed-switch matrix.
- Explicit algebraic square mapping from physical switches to chess coordinates.
- Debounced sensor events for piece presence and removal.
- Setup verification before starting a game.
- Runtime board resynchronization when the physical state does not match the virtual board.

### Local chess logic

- Internal virtual board state.
- FEN generation.
- PGN recording.
- Legal move validation.
- Captures and en passant support.
- Promotion handling through the web UI.
- Check and checkmate detection.
- Stalemate/draw lock.
- Draw offer, accept, and reject workflow.

### LED feedback

- Piece-present indication.
- Lifted-piece origin indication.
- Legal move highlighting.
- Best-move highlighting when StockfishOnline is enabled.
- Invalid move highlighting.
- Check indication using a red X pattern.
- Checkmate indication with winner/loser side colors.
- Optional empty-square LEDs with configurable color.
- Configurable runtime LED colors and brightness.

### Web interface

The ESP32 hosts a local web UI available from the fixed SoftAP address:

```text
http://192.168.4.1/
```

The interface includes:

- Board tab for live gameplay.
- Configuration tab for runtime settings.
- Physical board visualization.
- Turn display.
- FEN and PGN display.
- Legal moves.
- StockfishOnline raw JSON.
- Captured material and score.
- Draw controls.
- Chess clocks for both players.

### Runtime configuration

The Configuration tab supports:

- Invalid-position infraction enable/disable.
- Empty-square LED enable/disable.
- LED brightness.
- LED color customization.
- StockfishOnline enable/disable.
- Stockfish depth from 10 to 15.
- Chess clock base time.
- Per-move clock bonus/increment.
- Default configuration reset.

Runtime settings are stored in NVS so they survive reboot.

---

## 3. Hardware target

### Microcontroller

```text
ESP32-S3
ESP-IDF v5.3.x
Serial port: /dev/ttyACM0
```

### Reed-switch matrix pinout

Rows / ranks 1 to 8:

```text
GPIO4, GPIO5, GPIO6, GPIO7, GPIO8, GPIO9, GPIO10, GPIO11
```

Columns / files A to H:

```text
GPIO12, GPIO13, GPIO14, GPIO15, GPIO16, GPIO17, GPIO18, GPIO21
```

Scan model:

- Columns are configured as outputs.
- One column is driven HIGH at a time.
- Rows are inputs with pulldown.
- A HIGH row during a column scan indicates a closed reed switch.

### LED strip

Current firmware configuration:

```text
Data GPIO: GPIO38
Configured physical LEDs: 150
Used board LEDs: 64
Skipped LEDs: 35
```

Physical LED order from the first LED:

```text
H1-H8, skip 5,
G8-G1, skip 5,
F1-F8, skip 5,
E8-E1, skip 5,
D1-D8, skip 5,
C8-C1, skip 5,
B1-B8, skip 5,
A8-A1
```

This mapping allows the LED strip wiring to follow the physical board layout while keeping the firmware representation in algebraic coordinates.

---

## 4. Network architecture

The firmware runs in APSTA mode.

### Fixed SoftAP

```text
SSID: XADREZ_ESP
Password: xadrez12345
Web UI: http://192.168.4.1/
```

### WPA2 Enterprise STA

The STA interface supports WPA2 Enterprise for institutional Wi-Fi. Real credentials are not stored in source code and must not be committed to the repository.

Credential provisioning is handled locally with:

```bash
run chess provision
```

The command creates encrypted local credential artifacts under `secrets/`, generates a temporary NVS image, flashes it to the ESP32-S3 NVS partition, and deletes temporary plaintext files.

---

## 5. Firmware architecture

The firmware is organized around a small set of static FreeRTOS tasks and explicit ownership boundaries.

### Task model

| Task | Responsibility |
|---|---|
| `sensor_task` | Scans the reed-switch matrix and publishes sensor events. |
| `game_task` | Owns game state, move validation, captures, infractions, check/checkmate/draw logic, clocks, and LED frame generation. |
| `led_task` | Consumes LED command frames and renders the LED strip. |
| `network_task` | Initializes APSTA networking and starts the local HTTP server. |
| `stockfish_task` | Performs asynchronous StockfishOnline HTTP requests when enabled. |

### State ownership

- The game controller owns the virtual chess state.
- Shared game state is protected by `stateMutex`.
- HTTP handlers read protected snapshots or enqueue commands.
- Hardware drivers do not directly modify chess logic.
- LED rendering receives compact command frames rather than reading game internals directly.

This separation keeps the firmware maintainable and reduces cross-module coupling.

---

## 6. Chess clock behavior

The firmware includes configurable chess clocks for both players.

Configuration parameters:

- Base time in minutes.
- Per-move bonus/increment in seconds.

Runtime behavior:

- Both clocks are initialized when the game starts.
- The side to move counts down while the game is active.
- The clock continues during lifted-piece and promotion-pending states.
- After a legal move is completed, the moving side receives the configured bonus.
- If a player's clock reaches zero, that player loses immediately by time.

Terminal states:

```text
WHITE_TIME_LOSS
BLACK_TIME_LOSS
```

---

## 7. StockfishOnline advisor

The project uses an optional StockfishOnline advisor.

Behavior:

- Can be enabled or disabled from the Configuration tab.
- Depth is configurable from 10 to 15.
- Requests are asynchronous and non-blocking.
- The best move is shown on the web UI and highlighted in green on the board LEDs.
- Raw StockfishOnline JSON is displayed on the web interface.
- When disabled, pending requests and old best-move LED hints are cleared.

The firmware does not depend on StockfishOnline for move legality. Legal move validation is performed locally.

---

## 8. Game states and feedback

Important game states include:

| State | Meaning |
|---|---|
| `SETUP_NOT_READY` | Physical board does not match the initial setup. |
| `RUNNING_WAIT_FIRST_WHITE` | Game is ready and waiting for the first white move. |
| `PIECE_LIFTED` | A valid piece has been lifted. |
| `PROMOTION_PENDING` | A pawn promotion move needs piece selection. |
| `INVALID_MOVE` | A move was rejected by the local rule engine. |
| `CHECK` | The side to move is in check. |
| `CHECKMATE` | The game ended by checkmate. |
| `STALEMATE` | The game ended by stalemate. |
| `DRAW` | The game ended by accepted draw. |
| `WHITE_TIME_LOSS` | White lost on time. |
| `BLACK_TIME_LOSS` | Black lost on time. |

---

## 9. Source layout

```text
main/include       Public component headers
main/src/app       Application entry point, runtime config, and task creation
main/src/chess     Chess rules and compatibility engine module
main/src/drivers   Reed matrix and LED strip drivers
main/src/game      Game controller, web UI, state orchestration
main/src/net       Networking, credentials, provisioning, StockfishOnline client
main/src/tools     Firmware utility modes
```

---

## 10. Build and execution workflow

Build only:

```bash
run chess build
```

Build, flash, and monitor:

```bash
run chess
```

Monitor only:

```bash
run chess monitor
```

Credential provisioning:

```bash
run chess provision
```

---

## 11. Repository policy

Development branch:

```text
Breno
```

Repository rules:

- Keep `main` stable until the refactor is ready to be merged.
- Perform active development on `Breno`.
- Do not commit build output.
- Do not commit `secrets/`.
- Do not commit logs, temporary files, generated NVS images, or real credentials.
- Keep code, comments, identifiers, logs, commit messages, filenames, and technical documentation in English.

---

## 12. Results

The current implementation demonstrates a complete embedded chessboard workflow:

1. The physical board detects pieces through reed switches.
2. The ESP32-S3 validates moves locally.
3. LEDs provide immediate visual feedback.
4. The local web UI mirrors the physical game state.
5. FEN and PGN are updated during gameplay.
6. StockfishOnline can provide optional best-move guidance.
7. Runtime settings can be changed without recompiling firmware.
8. Chess clocks enforce time-based losses.

The result is an integrated cyber-physical chess system suitable for demonstrations, embedded systems learning, and interactive chess assistance.

---

## 13. Future work

Possible improvements:

- Improve mechanical enclosure and piece magnet alignment.
- Add per-square calibration diagnostics for the reed matrix.
- Add optional sound feedback.
- Add persistent PGN export.
- Add a compact mobile-first version of the web interface.
- Improve visual accessibility through high-contrast LED profiles.
- Add automated hardware self-test screens.

---

## 14. Conclusion

The ESP32-S3 Electronic Chessboard integrates embedded hardware, chess rules, LED rendering, APSTA networking, a browser-based UI, configurable clocks, and optional StockfishOnline analysis. The project demonstrates a complete embedded system with real-time sensing, local decision logic, user feedback, and network-based interaction while keeping firmware responsibilities separated and maintainable.
