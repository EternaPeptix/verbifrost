# Makefile for VerbifrostConnectX.kext
#
# Builds the kext without Xcode using the command-line tools.
# Requires: macOS with Xcode Command Line Tools installed.
#
# Usage:
#   make            # build the kext
#   make load       # load the kext (requires SIP disabled)
#   make unload     # unload the kext
#   make clean

KEXT_NAME    = VerbifrostConnectX
KEXT_BUNDLE  = com.verbifrost.connectx
KEXT_VERSION = 0.1

# Toolchain
CC       = clang++
SDKROOT  = $(shell xcrun --show-sdk-path 2>/dev/null)
KERNEL_HDR = $(SDKROOT)/System/Library/Frameworks/Kernel.framework/Headers
ARCH     = arm64e

# Paths
BUILD_DIR    = build
KEXT_DIR     = $(BUILD_DIR)/$(KEXT_NAME).kext
CONTENTS_DIR = $(KEXT_DIR)/Contents

# Sources
SRCS = VerbifrostConnectXRDMA.cpp mlx5_cmd.cpp mlx5_verbs.cpp mlx5_qp.cpp mlx5_mr.cpp

# Flags
CFLAGS = -arch $(ARCH) \
         -mkernel \
         -isysroot $(SDKROOT) \
         -I$(KERNEL_HDR) \
         -I$(KERNEL_HDR)/IOKit \
         -I./include \
         -I. \
         -Wall -Wextra \
         -std=c++17 \
         -fno-exceptions -fno-rtti \
         -fapple-kext \
         -DKERNEL \
         -DKERNEL_PRIVATE \
         -DRTAPI \
         -O2 -g

LDFLAGS = -arch $(ARCH) \
          -mkernel \
          -isysroot $(SDKROOT) \
          -nostdlib \
          -lkmod \
          -lkmodc++ \
          -Wl,-kext

# Targets
.PHONY: all clean load unload info

all: $(KEXT_DIR)/Contents/MacOS/$(KEXT_NAME)

$(KEXT_DIR)/Contents/MacOS/$(KEXT_NAME): $(SRCS) mlx5_registers.h mlx5_cmd.h
	@mkdir -p $(KEXT_DIR)/Contents/MacOS
	@mkdir -p $(KEXT_DIR)/Contents/Resources
	$(CC) $(CFLAGS) $(SRCS) -o $@ $(LDFLAGS)
	@cp VerbifrostConnectX.kext/Contents/Info.plist $(KEXT_DIR)/Contents/
	@echo "Built $(KEXT_NAME).kext"

load: all
	@echo "Loading kext (requires SIP disabled and sudo)..."
	sudo kextload -v $(KEXT_DIR)

unload:
	sudo kextunload -v -b $(KEXT_BUNDLE)

info:
	@echo "Kext bundle: $(KEXT_BUNDLE)"
	@echo "Architecture: $(ARCH)"
	@echo "SDK: $(SDKROOT)"

clean:
	rm -rf $(BUILD_DIR)
