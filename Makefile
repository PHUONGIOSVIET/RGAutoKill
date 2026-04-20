# ─── Theos Makefile ──────────────────────────────────────────────────────────
# Yêu cầu: Theos cài sẵn (https://theos.dev/docs/installation)
# Build:   make
# Deploy:  make package install  (cần SSH vào thiết bị)

TARGET    := iphone:clang:latest:14.0
ARCHS     := arm64
INSTALL_TARGET_PROCESSES := SoulKnightPrequel
BUNDLE_ID := com.chillyroom.soulknightprequel.gl

include $(THEOS)/makefiles/common.mk

TWEAK_NAME := RGAutoKill

# Source files
$(TWEAK_NAME)_FILES  := Tweak.mm

# Compiler flags
$(TWEAK_NAME)_CFLAGS := \
    -fobjc-arc \
    -std=c++17 \
    -O2 \
    -I$(THEOS_VENDOR_INCLUDE) \
    -I./vendor/imgui \
    -I./vendor/lua/include

# Linker flags
$(TWEAK_NAME)_LDFLAGS := \
    -lc++ \
    -framework Metal \
    -framework MetalKit \
    -framework UIKit \
    -framework Foundation \
    -L./vendor/lua/lib \
    -llua5.4

# Link against CydiaSubstrate (hooking)
$(TWEAK_NAME)_LIBRARIES  := substrate
$(TWEAK_NAME)_FRAMEWORKS := Metal MetalKit UIKit Foundation

# ImGui source files (vendored)
$(TWEAK_NAME)_FILES += \
    vendor/imgui/imgui.cpp \
    vendor/imgui/imgui_draw.cpp \
    vendor/imgui/imgui_tables.cpp \
    vendor/imgui/imgui_widgets.cpp \
    vendor/imgui/backends/imgui_impl_metal.mm \
    vendor/imgui/backends/imgui_impl_uikit.mm

include $(THEOS_MAKE_PATH)/tweak.mk
