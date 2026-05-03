GSDK_PATH ?= "/gecko_sdk/"

BUILD_TYPES := $(shell find . -maxdepth 1 -type d | cut -c 3- | tail -n +2)

# In order to build a custom image we need two things:
# 1. Build tools, either natively installed or a docker and a silabs-builder image.
# 2. A subdirectory containing the needed files for the custom build.
define test_tools
  @commander --version > /dev/null 2>&1; \
  if [ $$? != 0 ]; then \
    echo "Error: Prerequisites are not satified."; \
    echo "See Prerequisites in readme.md for more information"; \
    exit 1; \
  fi
endef

define test_build_type
  $(call test_tools)
  @if test "$(BUILD)x" = "x" -o ! -d "$(BUILD)"; then \
    if test "$(BUILD)x" = "x"; then \
      echo 'Error: Build type not specified.'; \
    else \
      echo "Error: subdirectory \"$(BUILD)\" does not exist."; \
    fi; \
    echo -n "  Available build types:"; \
    for choice in $(BUILD_TYPES);do echo -n " $$choice";done;\
    echo; \
    echo 'See "make help" for more information'; \
    exit 1; \
  fi
endef

DEVICE := $(shell if test -e $(BUILD)/firmware.slcp; then cat $(BUILD)/firmware.slcp | grep "id:[[:space:]]*EFR32" |  sed -E  's/.+id:[[:space:]]*EFR32([[:alnum:]]+).*}/EFR32\1/g'; fi)

BOOTLOADER_DIR=../bootloader
FULL_BINARIES_DIR=../full_binaries/v$(THIS_VER)
OTA_DIR=../ota_files
BUILD_DIR=custom/$(BUILD)
FW_OUT_DIR=$(BUILD_DIR)/out
BL_FW_OUT_DIR=$(BUILD_DIR)/bl_out

APP_S37=$(FW_OUT_DIR)/build/debug/${BUILD}.s37
BL_S37=$(BL_FW_OUT_DIR)/build/debug/${BUILD}.s37
FULL_S37=$(FULL_BINARIES_DIR)/$(BUILD)_FULL_v$(THIS_VER).s37
OTA_GBL=$(OTA_DIR)/upgrade_$(BUILD)_v$(THIS_VER).ota.bin
FW_SLPC_FILE=$(BUILD)/firmware.slcp
BL_SLPC_FILE=$(BUILD)/bootloader.slcp
SUBMODULE_TEST_FILE=../firmware/shared/README.md

GIT_VERSION := $(shell git describe --abbrev=4 --dirty --always --tags --all --match releases/*)
THIS_VER := $(shell if test -e $(FW_SLPC_FILE);then cat $(FW_SLPC_FILE) | sed 's/\r$$//' | awk '/SL_APPLICATION_VERSION/ {gsub(/'"'"'|"|\}|\s/, "", $$5);print $$5}';fi)


.PHONY: build clean veryclean bootloader firmware reset flash version help

build: $(SUBMODULE_TEST_FILE) $(FULL_BINARIES_DIR) $(OTA_DIR) $(FULL_S37) $(OTA_GBL)

clean: 
	$(call test_build_type)
	rm -rf $(FW_OUT_DIR) $(FULL_S37) $(OTA_GBL)

veryclean: clean
	rm -rf $(BL_FW_OUT_DIR)

$(BL_S37):
	$(call test_build_type)
	@echo "Building bootloader..."
	slc generate -v DEBUG -s $(GSDK_PATH) -p $(BL_SLPC_FILE) -o makefile -d $(BL_FW_OUT_DIR) -cp
	sed -i 's/C_DEFS   =/C_DEFS ?=/' ${BL_FW_OUT_DIR}/$(BUILD).Makefile
	# Add capability for flash enable pin
	cp $(BOOTLOADER_DIR)/btl_callbacks_stub_override.c ${BL_FW_OUT_DIR}/gecko_sdk_4.4.1/platform/bootloader/storage/btl_callbacks_stub.c
	# Path the flash driver to deal with inconsistent PDP tables in FMchip-sourced NOR flash
	cp $(BOOTLOADER_DIR)/btl_storage_spiflash_sfdp_patched.c ${BL_FW_OUT_DIR}/gecko_sdk_4.4.1/platform/bootloader/storage/spiflash_sfdp/btl_storage_spiflash_sfdp.c
	C_DEFS=-DBTL_TYPE_CUSTOM make -C $(BL_FW_OUT_DIR) -f $(BUILD).Makefile -j8
	@echo "Bootloader built successfully"


$(APP_S37):
	$(call test_build_type)
	@echo "Building firmware..."
	rm -rf $(FW_OUT_DIR)
	slc generate -v DEBUG -s $(GSDK_PATH) -p $(FW_SLPC_FILE) -o makefile -d $(FW_OUT_DIR) -cp
	sed -i 's/C_DEFS   =/C_DEFS ?=/' ${FW_OUT_DIR}/$(BUILD).Makefile
	C_DEFS=-DGIT_COMMIT_ID=\\\"$(GIT_VERSION)\\\" make -C $(FW_OUT_DIR) -f $(BUILD).Makefile -j8
	@echo "Firmware built successfully"

$(FULL_S37): $(BL_S37) $(APP_S37)
	commander convert -o $(FULL_S37) $(BL_S37) $(APP_S37)

$(OTA_GBL):
	commander gbl create $(OTA_GBL) --app $(APP_S37) --compress lzma --metadata ../firmware/fwid.bin

bootloader: $(BL_S37)

firmware: $(APP_S37)

reset:
	commander device reset

flash: $(FULL_S37)
	commander flash -d $(DEVICE) $(FULL_S37)

erase: 
	commander device masserase -d $(DEVICE)

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

$(FULL_BINARIES_DIR):
	mkdir -p $(FULL_BINARIES_DIR)

$(OTA_DIR):
	mkdir -p $(OTA_DIR)

$(SUBMODULE_TEST_FILE):
	git submodule update --init --recursive


