#---------------------------------------------------------------------------------
# Adapted from switch/native/Makefile (itself adapted from devkitPro's
# switch-examples/templates/application Makefile). Needs devkitA64 +
# libnx + deko3d (i.e. `sudo pacman -S switch-dev deko3d`).
#---------------------------------------------------------------------------------
.SUFFIXES:

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libnx/switch_rules

# Same pre-split path problem fixed in game/Makefile (audit 2026-08-24):
# ../../recomp/include only exists in the old monorepo layout, so this
# could not build from a fresh clone. Resolved against TOPDIR because
# this makefile re-invokes itself from inside $(BUILD).
# Resolution is now delegated to tools/fetch-conquertron.sh, which tries an
# explicit $(CONQUERTRON), then a sibling checkout, then a vendored copy under
# deps/, fetching it only if none of those exist. A sibling checkout
# deliberately beats the vendored copy: conquertron and jouster are developed
# together and a fetched tree silently shadowing local recompiler edits would
# mean the next hardware run tests the wrong code.
#
# CONQUERTRON is passed through the environment because a variable set on the
# make command line is not automatically visible to $(shell).
CONQUERTRON	?=
CONQUERTRON_DIR	:=	$(shell CONQUERTRON='$(CONQUERTRON)' $(TOPDIR)/../tools/fetch-conquertron.sh)

ifeq ($(CONQUERTRON_DIR),)
$(error could not resolve conquertron. Clone https://github.com/Arkchemy/conquertron next to this repo, or run: make CONQUERTRON=/path/to/conquertron -- see tools/fetch-conquertron.sh)
endif
ifeq ($(wildcard $(CONQUERTRON_DIR)/include/ppc_runtime.h),)
$(error conquertron at $(CONQUERTRON_DIR) has no include/ppc_runtime.h)
endif

TARGET		:=	ArkchemyGx2Test
BUILD		:=	build
SOURCES		:=	source $(CONQUERTRON_DIR)/include
DATA		:=	data
INCLUDES	:=	include $(CONQUERTRON_DIR)/include

APP_TITLE	:=	Arkchemy GX2 Test
APP_AUTHOR	:=	Arkchemy project

#---------------------------------------------------------------------------------
ARCH	:=	-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE

CFLAGS	:=	-g -Wall -O2 -ffunction-sections \
			$(ARCH) $(DEFINES)

CFLAGS	+=	$(INCLUDE) -D__SWITCH__

CXXFLAGS	:= $(CFLAGS) -fno-rtti -fno-exceptions

ASFLAGS	:=	-g $(ARCH)
LDFLAGS	=	-specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS	:= -ldeko3d -lnx -lm

LIBDIRS	:= $(PORTLIBS) $(LIBNX)

#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export TOPDIR	:=	$(CURDIR)

export VPATH	:=	$(foreach dir,$(SOURCES),$(if $(filter /%,$(dir)),$(dir),$(CURDIR)/$(dir))) \
			$(foreach dir,$(DATA),$(if $(filter /%,$(dir)),$(dir),$(CURDIR)/$(dir)))

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

ifeq ($(strip $(CPPFILES)),)
	export LD	:=	$(CC)
else
	export LD	:=	$(CXX)
endif

export OFILES_BIN	:=	$(addsuffix .o,$(BINFILES))
export OFILES_SRC	:=	$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES 	:=	$(OFILES_BIN) $(OFILES_SRC)
export HFILES_BIN	:=	$(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(if $(filter /%,$(dir)),$(dir),$(CURDIR)/$(dir))) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(CURDIR)/$(BUILD)

export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

export APP_JSON :=
export APP_ICON := $(CURDIR)/../native/icon.jpg
export NROFLAGS += --icon=$(APP_ICON) --nacp=$(CURDIR)/$(TARGET).nacp

.PHONY: $(BUILD) clean all

all: $(BUILD)

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).nro $(TARGET).nacp $(TARGET).elf

#---------------------------------------------------------------------------------
else
.PHONY:	all

DEPENDS	:=	$(OFILES:.o=.d)

all	:	$(OUTPUT).nro

$(OUTPUT).nro	:	$(OUTPUT).elf $(OUTPUT).nacp

$(OUTPUT).elf	:	$(OFILES)

$(OFILES_SRC)	: $(HFILES_BIN)

-include $(DEPENDS)

endif
