# ESP32-S3 Electronic Chessboard

<p align="center">
  <a href="./README.md"><b>README</b></a> ·
  <a href="./REPORT.md"><b>REPORT</b></a> ·
  <a href="./LICENSE"><b>LICENSE</b></a>
</p>

Professional firmware for an ESP32-S3 electronic chessboard with reed-switch board sensing, addressable LED feedback, WPA2 Enterprise station networking, fixed SoftAP access, a local web interface, runtime configuration, and optional StockfishOnline move hints.

The firmware is designed for a physical chessboard where magnetic pieces close reed switches under the squares. The ESP32-S3 maintains a virtual chess position, validates moves, renders board feedback through LEDs, and exposes the game state through a browser-accessible interface at `http://192.168.4.1/`.

## Current development status

Development is currently performed on the `Breno` branch. The `main` branch should remain stable until the ongoing refactor is ready to be merged.

Repository:

```text
https://github.com/Breno-Sanchez/electronic-chess
```

## Main features

- ESP32-S3 firmware using ESP-IDF v5.3.x.
- Reed-switch matrix scan for physical piece detection.
- Virtual chessboard state with FEN and PGN tracking.
- Legal move validation, captures, check, checkmate, draw proposal flow, and stalemate detection.
- Optional invalid-position infraction handling.
- WS2812-style LED feedback through the ESP32-S3 RMT peripheral.
- Fixed SoftAP web interface at `http://192.168.4.1/`.
- WPA2 Enterprise STA support for institutional Wi-Fi.
- APSTA mode: the board can connect to Wi-Fi while still serving its own local access point.
- Runtime web configuration for LED brightness, LED colors, empty-square LEDs, Stockfish enable/disable, and Stockfish depth.
- Optional StockfishOnline REST advisor with selectable depth from 10 to 15.
- Local browser UI with board visualization, game state, FEN, PGN, legal moves, best move hints, captured material, infractions, check/checkmate/draw status, and raw Stockfish JSON.

## Hardware target

| Item | Value |
| --- | --- |
| MCU | ESP32-S3 |
| Framework | ESP-IDF v5.3.x |
| Serial port | `/dev/ttyACM0` |
| LED output | GPIO38 |
| Reed matrix rows | GPIO4, GPIO5, GPIO6, GPIO7, GPIO8, GPIO9, GPIO10, GPIO11 |
| Reed matrix columns | GPIO12, GPIO13, GPIO14, GPIO15, GPIO16, GPIO17, GPIO18, GPIO21 |

## Network behavior

The firmware uses APSTA mode.

### Fixed SoftAP

| Setting | Value |
| --- | --- |
| SSID | `XADREZ_ESP` |
| Password | `xadrez12345` |
| Local UI | `http://192.168.4.1/` |

The SoftAP settings are intentionally fixed so that the board always has a predictable local access path.

### WPA2 Enterprise STA

WPA2 Enterprise support must remain enabled. Institutional credentials must never be committed to the repository.

Credentials are provisioned into NVS locally and are not stored in source code. Use placeholders or local provisioning only.

## Reed-switch matrix

The matrix uses columns as outputs and rows as inputs.

- Columns A to H are driven HIGH one at a time.
- Rows 1 to 8 use pulldown inputs.
- A HIGH row reading during a column scan means the reed switch at that square is closed.
- Squares are represented with algebraic names such as `a1`, `e4`, and `h8`.

### Pinout

| Board line | GPIO |
| --- | --- |
| Row 1 | GPIO4 |
| Row 2 | GPIO5 |
| Row 3 | GPIO6 |
| Row 4 | GPIO7 |
| Row 5 | GPIO8 |
| Row 6 | GPIO9 |
| Row 7 | GPIO10 |
| Row 8 | GPIO11 |

| Board file | GPIO |
| --- | --- |
| Column A | GPIO12 |
| Column B | GPIO13 |
| Column C | GPIO14 |
| Column D | GPIO15 |
| Column E | GPIO16 |
| Column F | GPIO17 |
| Column G | GPIO18 |
| Column H | GPIO21 |

## LED strip

The board uses the ESP32-S3 RMT peripheral to drive the LED strip.

| Item | Value |
| --- | --- |
| Data GPIO | GPIO38 |
| Used board LEDs | 64 |
| Skipped LEDs | 35 |
| Minimum physical LEDs | 99 |
| Configured physical LEDs | 150 |

### Physical LED order

The physical LED strip order is intentionally explicit because the strip is routed through the board with skipped LEDs between files.

```text
H1 to H8, skip 5 unused LEDs,
G8 to G1, skip 5 unused LEDs,
F1 to F8, skip 5 unused LEDs,
E8 to E1, skip 5 unused LEDs,
D1 to D8, skip 5 unused LEDs,
C8 to C1, skip 5 unused LEDs,
B1 to B8, skip 5 unused LEDs,
A8 to A1.
```

### Default LED semantics

| Board state | LED behavior |
| --- | --- |
| Empty square | Off by default, or configurable color if empty-square LEDs are enabled |
| Piece present | Weak blue |
| Lifted origin square | Blinking weak blue |
| Legal moves | Yellow |
| Stockfish best move | Green |
| Invalid move | Red |
| Check | Red check indication |
| Draw | Configurable draw color |
| Checkmate | Winner side blinks green, loser side blinks red |

The web Configuration tab allows runtime changes to brightness and LED colors.

## Web interface

The local UI is served from the SoftAP address:

```text
http://192.168.4.1/
```

The UI includes:

- Live board view.
- Physical presence map.
- Current side to move.
- Current FEN.
- Current PGN.
- Selected/lifted piece state.
- Legal moves for the lifted piece.
- Optional StockfishOnline best move.
- Raw StockfishOnline JSON response.
- Captured material and material points.
- Invalid-position infractions.
- Check, checkmate, draw, and stalemate status.
- Draw proposal controls for both sides.
- Runtime Configuration tab.

### Runtime configuration

The Configuration tab supports:

- Invalid-position infractions on/off.
- Empty-square LEDs on/off.
- StockfishOnline advisor on/off.
- Stockfish depth selection from 10 to 15.
- LED brightness from 0% to 100%.
- Runtime color selection for empty, piece, lifted, legal, best, invalid, check, and draw states.
- Default configuration reset.

Runtime configuration is stored in NVS. Default values are derived from `main/config.yaml`.

## Chess behavior

The firmware maintains the authoritative virtual chessboard state.

Supported state tracking:

- FEN.
- PGN.
- Side to move.
- Castling rights.
- En passant target.
- Halfmove/fullmove counters.
- Captured material.
- Check and checkmate.
- Draw by agreement.
- Stalemate.
- Promotion flow through the web UI.

Physical movement model:

1. A player lifts a piece from the physical board.
2. The firmware maps the physical square to a virtual square.
3. Legal destination squares are highlighted.
4. The player places the piece on a target square.
5. The move is validated by the local chess rules engine.
6. If legal, the virtual board, FEN, PGN, LEDs, and UI state are updated.
7. If illegal, the involved squares are marked invalid and infractions may be counted depending on runtime configuration.

## StockfishOnline advisor

The firmware can optionally request move hints from StockfishOnline.

Runtime controls:

- Enable or disable StockfishOnline from the Configuration tab.
- Select depth from 10 to 15.
- Disable StockfishOnline to remove green best-move indications.

The Stockfish advisor is not required for move legality. Legal move validation is performed locally by the firmware.

## Configuration file

Default firmware settings are kept in:

```text
main/config.yaml
```

Typical settings include:

- Project name.
- Target MCU.
- Serial port.
- SoftAP settings.
- StockfishOnline URL, default enable state, timeout, response size, and depth.
- LED GPIO, physical LED count, brightness, and default colors.

Do not store real institutional Wi-Fi credentials in `config.yaml`.

## Build and flash workflow

The preferred project command is:

```bash
run chess
```

It loads the ESP-IDF environment, enters the project directory, sets the target to `esp32s3`, cleans the previous build when configured by the helper, builds the firmware, flashes `/dev/ttyACM0`, and opens the serial monitor.

### Common commands

Build only:

```bash
run chess build
```

This compiles the firmware without flashing the board.

Build, flash, and monitor:

```bash
run chess
```

This compiles the firmware, flashes the ESP32-S3 through `/dev/ttyACM0`, and opens the serial monitor.

Monitor only:

```bash
run chess monitor
```

This opens the serial monitor without rebuilding or flashing.

Provision WPA2 Enterprise credentials:

```bash
run chess provision
```

This asks for SSID, EAP identity, username, and password locally, creates an encrypted local archive under `secrets/`, generates a temporary NVS image, flashes it to the ESP32-S3 NVS partition, and deletes temporary plaintext files.

## Source layout

```text
main/include       Public component headers
main/src/app       Application startup, runtime configuration, and task creation
main/src/chess     Local chess rules, board state, FEN, PGN, and move validation
main/src/drivers   Reed matrix scanner and LED strip renderer
main/src/game      Game controller, UI state, HTTP API, move orchestration, and LED command generation
main/src/net       Wi-Fi, credential provisioning, and StockfishOnline HTTP client
main/src/tools     Firmware-only utility modes such as LED mapping mode
```

## RTOS architecture

The firmware uses FreeRTOS tasks with static ownership boundaries.

| Task | Responsibility |
| --- | --- |
| `sensor_task` | Scans the reed matrix and publishes `sensor_event_t` messages |
| `game_task` | Owns game state, board state, move validation, captures, infractions, draw/check/checkmate/stalemate, and LED frame generation |
| `led_task` | Consumes `led_command_t` frames and renders the physical LED strip |
| `network_task` | Initializes APSTA networking and starts the local HTTP server |
| `stockfish_task` | Processes queued StockfishOnline requests when the advisor is enabled |

Game state must be protected by `stateMutex`. Hardware drivers should not directly mutate chess state.

## Repository policy

- Work on branch `Breno` during the current refactor.
- Keep `main` stable until the refactor is intentionally merged.
- Use English for code, comments, identifiers, logs, commit messages, file names, and technical documentation.
- Do not commit build output, logs, generated NVS images, temporary files, local archives, or credentials.
- Do not remove WPA2 Enterprise support.
- Do not remove the fixed SoftAP web interface.
- Keep dependencies minimal.
- Prefer compact, maintainable, MISRA-C-oriented C code.

## Security notes

- Never commit real institutional credentials.
- Keep local credential archives under `secrets/` out of Git.
- The SoftAP password is fixed for this hardware project, but institutional STA credentials must be provisioned locally.
- StockfishOnline is optional and can be disabled at runtime.

## Diagnostics

Useful serial logs include:

- `SENSOR`: physical reed matrix changes.
- `SERVER: EVENT`: processed physical board events.
- `SERVER: STATE`: game-state transitions.
- `SERVER: MOVE_TRY`: attempted move mapping.
- `SERVER: MOVE_OK`: accepted legal move.
- `SERVER: MOVE_REJECT`: rejected illegal move.
- `SERVER: BEST_REQUEST`: StockfishOnline request queued.
- `SERVER: BEST_HINT`: StockfishOnline response accepted.
- `SERVER: BEST_DROP`: stale StockfishOnline response ignored.

## License

This repository currently reports a non-standard or unspecified license. Add a formal license file before public reuse beyond the project team.
