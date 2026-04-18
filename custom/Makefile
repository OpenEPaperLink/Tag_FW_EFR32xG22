:COMMANDER ?= ~/.local/bin/commander/commander
GSDK_PATH ?= "/gecko_sdk/"

BUILD_TYPES=$(shell find . -maxdepth 1 -type d | cut -c 3- | tail -n +2)

define test_build_type
  @if test "$(BUILD)x" = "x" -o ! -d "$(BUILD)"; then \
    if test "$(BUILD)x" = "x"; then \
      echo 'Error: Build type not specified.'; \
    else \
      echo "Error: subdirectory \"$(BUILD)\" does not exist."; \
    fi; \
    echo -n "  Availabile build types:"; \
    for choice in $(BUILD_TYPES);do echo -n " $$choice";done;\
    echo; \
    echo 'See "make help" for more information'; \
    exit 1; \
  fi
endef

DEVICE := $(shell if test -e $(BUILD)/firmware.slcp; then cat $(BUILD)/firmware.slcp | grep "id:[[:space:]]*EFR32" |  sed -E  's/.+id:[[:space:]]*EFR32([[:alnum:]]+).*}/EFR32\1/g'; fi)

BOOTLOADER_DIR=../bootloader
FULL_BINARIES_DIR=full_binaries/v$(THIS_VER)
OTA_DIR=ota_files
BUILD_DIR=custom/$(BUILD)
FW_OUT_DIR=$(BUILD_DIR)/out
BL_FW_OUT_DIR=$(BUILD_DIR)/bl_out

APP_S37=$(FW_OUT_DIR)/build/debug/${BUILD}.s37
BL_S37=$(BL_FW_OUT_DIR)/build/debug/${BUILD}.s37
FULL_S37=$(FULL_BINARIES_DIR)/$(BUILD)_FULL_v$(THIS_VER).s37
OTA_GBL=$(OTA_DIR)/upgrade_$(BUILD)_v$(THIS_VER).ota.bin
FW_SLPC_FILE=$(BUILD_DIR)/firmware.slcp
BL_SLPC_FILE=$(BUILD_DIR)/bootloader.slcp
SUBMODULE_TEST_FILE=../firmware/shared/README.md

GIT_VERSION := $(shell git describe --abbrev=4 --dirty --always --tags --all --match releases/*)
THIS_VER := $(shell if test -e ../$(FW_SLPC_FILE);then cat ../$(FW_SLPC_FILE) | sed 's/\r$$//' | awk '/SL_APPLICATION_VERSION/ {gsub(/'"'"'|"|\}|\s/, "", $$5);print $$5}';fi)

DOCKER_ARGS=run --rm -it --user builder -v $(CURDIR)/..:/build -v ~/.gitconfig:/home/builder/.gitconfig silabs-builder

FW_SLC_GEN_CMD=slc generate -v DEBUG -s $(GSDK_PATH) -p $(FW_SLPC_FILE) -o makefile -d $(FW_OUT_DIR) -cp
BL_SLC_GEN_CMD=slc generate -v DEBUG -s $(GSDK_PATH) -p $(BL_SLPC_FILE) -o makefile -d $(BL_FW_OUT_DIR) -cp
BUILD_FW_CMD=env C_DEFS=-DGIT_COMMIT_ID=\\\"$(GIT_VERSION)\\\" make -C $(FW_OUT_DIR) -f $(BUILD).Makefile -j8
BUILD_BL_CMD=env C_DEFS=-DBTL_TYPE_CUSTOM make -C $(BL_FW_OUT_DIR) -f $(BUILD).Makefile -j8
BUILD_FULL_CMD=commander convert -o $(FULL_S37) $(BL_S37) $(APP_S37)
BUILD_OTA_CMD=commander gbl create $(OTA_GBL) --app $(APP_S37) --compress lzma --metadata firmware/fwid.bin
FLASH_CMD=commander flash -d $(DEVICE) $(FULL_S37)
ERASE_CMD=commander device masserase -d $(DEVICE)
RESET_CMD=commander device reset

.PHONY: build clean veryclean bootloader firmware reset flash version help

build: $(SUBMODULE_TEST_FILE) ../$(FULL_BINARIES_DIR) ../$(OTA_DIR) ../$(FULL_S37) ../$(OTA_GBL)

clean: 
	$(call test_build_type)
	rm -rf ../$(FW_OUT_DIR) ../$(FULL_S37) ../$(OTA_GBL)

veryclean: clean
	rm -rf ../$(BL_FW_OUT_DIR)

../$(BL_S37):
	$(call test_build_type)
	@echo "Building bootloader..."
	docker $(DOCKER_ARGS) $(BL_SLC_GEN_CMD)
	sed -i 's/C_DEFS   =/C_DEFS ?=/' ../${BL_FW_OUT_DIR}/$(BUILD).Makefile
	# Add capability for flash enable pin
	cp $(BOOTLOADER_DIR)/btl_callbacks_stub_override.c ../${BL_FW_OUT_DIR}/gecko_sdk_4.4.1/platform/bootloader/storage/btl_callbacks_stub.c
	# Path the flash driver to deal with inconsistent PDP tables in FMchip-sourced NOR flash
	cp $(BOOTLOADER_DIR)/btl_storage_spiflash_sfdp_patched.c ../${BL_FW_OUT_DIR}/gecko_sdk_4.4.1/platform/bootloader/storage/spiflash_sfdp/btl_storage_spiflash_sfdp.c
	docker $(DOCKER_ARGS) $(BUILD_BL_CMD)
	@echo "Bootloader built successfully"


../$(APP_S37):
	$(call test_build_type)
	@echo "Building firmware..."
	rm -rf ../$(FW_OUT_DIR)
	docker $(DOCKER_ARGS) $(FW_SLC_GEN_CMD)
	sed -i 's/C_DEFS   =/C_DEFS ?=/' ../${FW_OUT_DIR}/$(BUILD).Makefile
	docker $(DOCKER_ARGS) $(BUILD_FW_CMD)
	@echo "Firmware built successfully"

../$(FULL_S37): ../$(BL_S37) ../$(APP_S37)
	docker $(DOCKER_ARGS) $(BUILD_FULL_CMD)

../$(OTA_GBL):
	docker $(DOCKER_ARGS) $(BUILD_OTA_CMD)

bootloader: ../$(BL_S37)

firmware: ../$(APP_S37)

reset:
	docker $(DOCKER_ARGS) $(RESET_CMD)

flash: ../$(FULL_S37)
	docker $(DOCKER_ARGS) $(FLASH_CMD)

erase: 
	docker $(DOCKER_ARGS) $(ERASE_CMD)

version:
	$(call test_build_type)
	@echo "GIT_VERSION: $(GIT_VERSION)"
	@echo "THIS_VER: $(THIS_VER)"

help:	
	@echo "Usage:"
	@echo "  make            - build full and OTA update custom images"
	@echo "  make clean      - clean app build, leaving bootloader"
	@echo "  make veryclean  - clean bootloader and app builds"
	@echo "  make flash      - flash image with full custom image"
	@echo "  make reset      - reset board"
	@echo "  make version    - display git and release versions"
	@echo "  make help       - help (you're looking at it)"
	@echo
	@echo "  Specify the BUILD type on the command line or set the environment variable BUILD."
	@echo '  For example "BUILD=example make flash"'

../$(FULL_BINARIES_DIR):
	mkdir -p ../$(FULL_BINARIES_DIR)

../$(OTA_DIR):
	mkdir -p ../$(OTA_DIR)

$(SUBMODULE_TEST_FILE):
	git submodule update --init --recursive


