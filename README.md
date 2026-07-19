# cami_embedded

ESP-IDF firmware.

## Layout

```
firmware/                       ESP-IDF project root
  CMakeLists.txt                top-level project definition
  partitions.csv                partition table (single factory app, no OTA)
  sdkconfig.defaults            defaults shared by every board
  sdkconfig.defaults.<board>    per-board overrides (flash size, console, ...)
  app/                          application component (app_main)
  board_hal/                    hardware abstraction layer component
  config/board.h                per-target pin/config definitions
  myo_ble/                      Myo armband BLE protocol + client (NimBLE)
tests/                          pytest-embedded suite (runs on real hardware)
Makefile                        build/flash/monitor/test/format shortcuts
```

## Prerequisites

- ESP-IDF is already installed at `~/dev/esp-idf` (see the `esp_idf_config` repo for
  install/update/flashing notes).
- Every new terminal needs the ESP-IDF environment sourced before `idf.py` or any
  `make` target that calls it will work:

  ```sh
  esp_idf
  ```

  That's the alias in `~/.bashrc` (`alias esp_idf='source ~/dev/esp-idf/export.sh'`).

- `clang-format`, for `make format` (`sudo apt install clang-format`).
- Python deps for the test suite: `pip install -r tests/requirements.txt`.

> **Heads up:** as of this setup, `esp_idf` fails to fully activate on this machine
> (`click`/`cryptography`/`pyparsing` in `~/.espressif/python_env/idf6.2_py3.12_env`
> are newer than the versions ESP-IDF v6.0.2 constrains to), so `idf.py` doesn't end
> up on `PATH`. That's a pre-existing environment issue, not something specific to
> this project — probably worth a `~/dev/esp-idf/install.sh` re-run to fix the venv.

## Selecting a board

The chip target is controlled by the `BOARD` variable (default `esp32`) and persists
in `firmware/sdkconfig` once set:

```sh
make board BOARD=esp32       # or esp32s3, esp32c3, ...
```

Only `esp32` is wired up in hardware today. `firmware/sdkconfig.defaults.esp32s3` is
stubbed out so the next board only needs its actual settings filled in; add its
pin/config definitions in `firmware/config/board.h` next to the existing `esp32` ones.

## Build / flash / monitor

```sh
make build                    # idf.py build
make flash PORT=/dev/ttyUSB0  # flash over serial (PORT auto-detects if omitted)
make monitor                  # serial monitor
make flash-monitor             # flash then monitor
make menuconfig                # interactive configuration
make clean                     # idf.py fullclean
make erase                     # erase the chip's flash
```

`PORT` auto-detects the first `/dev/ttyUSB*` or `/dev/ttyACM*` device if not given.

## Editor / clangd support

```sh
make link
```

Builds (if needed) and symlinks `firmware/build/compile_commands.json` into
`firmware/compile_commands.json` so clangd finds it. Re-run after adding files or
changing includes. `.vscode/settings.json` already points clangd there and disables
the built-in C/C++ IntelliSense engine to avoid conflicts.

## Formatting

```sh
make format
```

Runs `clang-format` over everything in `firmware/app`, `firmware/board_hal`, and
`firmware/config`. Style lives in `.clang-format`.

## Tests

Tests use [pytest-embedded](https://docs.espressif.com/projects/pytest-embedded/) to
flash the already-built firmware onto real hardware and check its serial output.

```sh
pip install -r tests/requirements.txt
make board BOARD=esp32
make build
make test PORT=/dev/ttyUSB0
```

`make test` runs `pytest tests --target $(BOARD) --app-path firmware --port $(PORT)`.
