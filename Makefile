# ===== Configuration =====

PROJECT_DIR := firmware

BOARD ?= esp32
PORT  ?= $(shell ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null | head -n 1)
BAUD  ?= 115200

IDF := idf.py -C $(PROJECT_DIR)

.DEFAULT_GOAL := help

# ======= Targets ========

.PHONY: help
help:
	@echo "BOARD=$(BOARD)  PORT=$(PORT)"
	@echo ""
	@echo "  make board          set the chip target (idf.py set-target)"
	@echo "  make build          build the firmware"
	@echo "  make flash          flash over serial"
	@echo "  make monitor        open the serial monitor"
	@echo "  make flash-monitor  flash then monitor"
	@echo "  make menuconfig     interactive configuration"
	@echo "  make clean          idf.py fullclean"
	@echo "  make erase          erase the chip's flash"
	@echo "  make link           (re)generate compile_commands.json for clangd"
	@echo "  make format         run clang-format over firmware sources"
	@echo "  make test           run the pytest suite against real hardware"
	@echo ""
	@echo "Override with e.g. 'make build BOARD=esp32s3' or 'make flash PORT=/dev/ttyUSB0'."

.PHONY: board
board:
	$(IDF) set-target $(BOARD)

.PHONY: menuconfig
menuconfig:
	$(IDF) menuconfig

.PHONY: build
build:
	$(IDF) build

.PHONY: flash
flash:
	$(IDF) -p $(PORT) flash

.PHONY: monitor
monitor:
# 	$(IDF) -p $(PORT) monitor
	picocom -b $(BAUD) $(PORT)

.PHONY: flash-monitor
flash-monitor:
	$(IDF) -p $(PORT) flash monitor

.PHONY: clean
clean:
	$(IDF) fullclean

.PHONY: erase
erase:
	esptool.py --chip $(BOARD) --port $(PORT) erase_flash

.PHONY: link
link: build
	ln -sf build/compile_commands.json $(PROJECT_DIR)/compile_commands.json

.PHONY: format
format:
	@command -v clang-format >/dev/null || { echo "clang-format not found (sudo apt install clang-format)"; exit 1; }
	find $(PROJECT_DIR)/app $(PROJECT_DIR)/board_hal $(PROJECT_DIR)/config $(PROJECT_DIR)/myo_ble \( -name '*.c' -o -name '*.h' -o -name '*.cpp' -o -name '*.hpp' \) -print0 \
		| xargs -0 clang-format -i

.PHONY: test
test:
	pytest tests --target $(BOARD) --app-path $(CURDIR)/$(PROJECT_DIR) --port $(PORT)
