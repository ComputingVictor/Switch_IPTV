ifeq ($(strip $(DEVKITPRO)),)
$(error "DEVKITPRO no esta definido. Instala devkitPro/devkitA64")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libnx/switch_rules

TARGET      := SwitchIPTV
BUILD       := build
SOURCES     := source
INCLUDES    := include
ROMFS       := resources
BOREALIS_PATH ?= external/borealis
USE_BOREALIS ?= 0
APP_TITLE   := SwitchIPTV
APP_AUTHOR  := SwitchIPTV contributors
APP_VERSION := 0.2.0
APP_ICON    := $(TOPDIR)/icon-iptvswitch.jpg

ARCH := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE
CFLAGS   := -g -Wall -O2 -ffunction-sections $(ARCH) $(DEFINES)
CFLAGS   += $(INCLUDE) -D__SWITCH__
CXXFLAGS := $(CFLAGS) -std=gnu++17
ASFLAGS  := -g $(ARCH)
LDFLAGS  := -specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)
LIBS     := -lSDL2 -lEGL -lglapi -ldrm_nouveau -lavformat -lavcodec -ldav1d \
            -lswscale -lswresample -lavutil -lcurl -lmbedtls -lmbedx509 -lmbedcrypto \
            -lfreetype -lbz2 -lpng16 -lz -lharfbuzz -lpthread -lm -lnx
LIBDIRS  := $(PORTLIBS) $(LIBNX)

ifeq ($(USE_BOREALIS),1)
ifeq ($(wildcard $(TOPDIR)/$(BOREALIS_PATH)/library/borealis.mk),)
$(error Borealis was not found at $(BOREALIS_PATH). Run: git submodule update --init --recursive)
endif
ROMFS := $(CURDIR)/build/borealis-romfs
include $(TOPDIR)/$(BOREALIS_PATH)/library/borealis.mk
CXXFLAGS += -DBOREALIS_ENABLED
CXXFLAGS += -include optional
CXXFLAGS += -DswkbdConfigSetStringLenMaxExt=swkbdConfigSetStringLenMax
OUT_SHADERS := shaders
endif

ifneq ($(BUILD),$(notdir $(CURDIR)))
export OUTPUT := $(CURDIR)/$(TARGET)
export TOPDIR := $(CURDIR)
export NROFLAGS += --icon=$(APP_ICON) --nacp=$(CURDIR)/$(TARGET).nacp
export VPATH := $(foreach dir,$(SOURCES),$(TOPDIR)/$(dir))
export DEPSDIR := $(TOPDIR)/$(BUILD)
CFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(TOPDIR)/$(dir)/*.c)))
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(TOPDIR)/$(dir)/*.cpp)))
ifeq ($(USE_BOREALIS),1)
APP_CPPFILES := $(notdir $(wildcard $(TOPDIR)/source/*.cpp))
CPPFILES := $(filter-out $(filter-out borealis_main.cpp,$(APP_CPPFILES)),$(CPPFILES))
CFILES := $(filter-out $(notdir $(wildcard $(TOPDIR)/source/*.c)),$(CFILES))
else
CPPFILES := $(filter-out borealis_main.cpp,$(CPPFILES))
endif
export OFILES := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o)
ifeq ($(strip $(CPPFILES)),)
export LD := $(CC)
else
export LD := $(CXX)
endif
export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) $(foreach dir,$(LIBDIRS),-I$(dir)/include) -I$(PORTLIBS)/include/freetype2
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)
.PHONY: all clean
ifeq ($(USE_BOREALIS),1)
all: borealis-resources $(BUILD)
borealis-resources:
	@mkdir -p $(ROMFS)
	@cp -R $(TOPDIR)/$(BOREALIS_PATH)/resources/. $(ROMFS)/
	@cp -R $(CURDIR)/resources/. $(ROMFS)/
else
all: $(BUILD)
endif
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile
$(BUILD):
	@mkdir -p $@
clean:
	@rm -fr $(BUILD) $(TARGET).nro $(TARGET).nacp $(TARGET).elf
else
DEPENDS := $(OFILES:.o=.d)
$(OUTPUT).nro: $(OUTPUT).elf $(OUTPUT).nacp
$(OUTPUT).elf: $(OFILES)
-include $(DEPENDS)
endif
