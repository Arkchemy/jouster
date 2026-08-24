#---------------------------------------------------------------------------------
# Arkchemy's first full-game build: the real, complete recompiled
# Skylanders: Spyro's Adventure C output (switch/game/source/generated_*.c,
# 213 files -- split from one real recomp run against the real
# tfbGame_cafe.rpx, since a single ~308MB/8.5M-line translation unit was
# too heavy to compile safely in one shot) linked against every real
# cafeos_*.h CafeOS runtime shim header, plus this project's own
# main.c (real entry point call + a real, independent progress
# indicator -- see main.c's own comment for why it doesn't depend on
# the recompiled game's own, still largely no-op, draw calls).
#
# -O0 deliberately for this first real build: fast, low-memory compiles
# across 213+ files are the real priority right now (does it link and
# run at all), not runtime speed -- a real, separate optimization pass
# once this actually boots.
#---------------------------------------------------------------------------------
.SUFFIXES:

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libnx/switch_rules

# Where conquertron (the recompiler) lives. It supplies ppc_runtime.h,
# the cafeos_*.h Cafe OS shims and cafeos_state.c, all of which the
# generated C is compiled against.
#
# Real issue found in an audit, 2026-08-24: this used to be hardcoded to
# ../../recomp/include, which is only correct in the old pre-split
# monorepo layout. After the org split that path does not exist, so the
# published repository could not be built by anyone who cloned it --
# make would simply not find a single header. Now a variable, defaulting
# to the obvious side-by-side clone layout:
#
#     some-dir/
#       conquertron/
#       jouster/        <- you are in jouster/game when running make
#
# Override it if your checkout differs:
#     make CONQUERTRON=/path/to/conquertron
# Resolved against TOPDIR, not CURDIR: this makefile re-invokes itself
# from inside $(BUILD), so a CURDIR-relative default silently points
# somewhere different on the second pass. TOPDIR is exported by the
# outer pass and stays correct in both.
CONQUERTRON	?=	$(TOPDIR)/../../conquertron
CONQUERTRON_DIR	:=	$(abspath $(CONQUERTRON))

ifeq ($(wildcard $(CONQUERTRON_DIR)/include/ppc_runtime.h),)
$(error conquertron headers not found at $(CONQUERTRON_DIR)/include. Clone https://github.com/Arkchemy/conquertron next to this repo, or run: make CONQUERTRON=/path/to/conquertron)
endif

TARGET		:=	Jouster
BUILD		:=	build
SOURCES		:=	source $(CONQUERTRON_DIR)/include
DATA		:=	data
INCLUDES	:=	include $(CONQUERTRON_DIR)/include

APP_TITLE	:=	Arkchemy -- Jouster
APP_AUTHOR	:=	Arkchemy Contributors

#---------------------------------------------------------------------------------
ARCH	:=	-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE

CFLAGS	:=	-g -Wall -O0 -ffunction-sections \
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

# $(if $(filter /%,...)): SOURCES/INCLUDES may contain absolute paths
# now that conquertron lives in a separate repository. Prefixing an
# absolute path with $(CURDIR)/ would produce a bogus location.
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
# main.c carries the __DATE__/__TIME__ build stamp printed as the first
# log line, which is how a hardware log identifies which .nro produced
# it. Without this touch the stamp goes stale whenever only generated_*.c
# changed, so a genuinely new binary reports an old build time -- exactly
# the ambiguity the stamp exists to remove. One extra compile out of 218.
	@touch $(CURDIR)/source/main.c
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
