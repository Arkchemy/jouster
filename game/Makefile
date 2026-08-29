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

# FFmpeg comes from the devkitPro portlibs and is already installed; it is not
# a new dependency and needs no forked tree. Two things it gives us:
#   - H.264 for meta/bootMovie.h264, including averne's Tegra hardware
#     backend (ff_h264_nvtegra_hwaccel is compiled into this build)
#   - a Bink decoder and demuxer (ff_bink_decoder, ff_binkaudio_*,
#     ff_bink_demuxer), so the game's own .mov files can be decoded
#     independently of the recompiled decoder -- useful both as a shortcut to
#     picture and sound, and as an ORACLE to diff against when debugging the
#     recompiled one.
# Link order matters: avformat before avcodec before avutil.
LIBS	:= -ldeko3d -lavformat -lavcodec -lswresample -lswscale -lavutil -ldav1d -lbz2 -lz -lnx -lm -lpthread

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

.PHONY: $(BUILD) clean all conquertron-info conquertron-update

all: $(BUILD)

# Which conquertron is this build actually compiling against? Worth being able
# to ask directly: a sibling checkout and a vendored copy look identical in the
# output, and building against the wrong one is silent.
conquertron-info:
	@echo "conquertron : $(CONQUERTRON_DIR)"
	@if [ -d "$(CONQUERTRON_DIR)/.git" ]; then \
		echo "commit      : $$(git -C '$(CONQUERTRON_DIR)' rev-parse HEAD)"; \
		echo "described   : $$(git -C '$(CONQUERTRON_DIR)' log --oneline -1)"; \
		if [ -n "$$(git -C '$(CONQUERTRON_DIR)' status --porcelain)" ]; then \
			echo "state       : LOCALLY MODIFIED (uncommitted changes are in this build)"; \
		else echo "state       : clean"; fi; \
	else echo "commit      : not a git checkout"; fi
	@case "$(CONQUERTRON_DIR)" in \
		*/jouster/deps/conquertron) echo "source      : vendored under deps/, pinned by conquertron.lock";; \
		*) echo "source      : local checkout -- your edits are what get built";; \
	esac

# Update the vendored copy to the latest upstream and re-pin the lock file.
# Refuses to touch a local checkout: pulling someone's working tree from under
# them is not this target's business.
conquertron-update:
	@case "$(CONQUERTRON_DIR)" in \
		*/jouster/deps/conquertron) ;; \
		*) echo "conquertron-update: $(CONQUERTRON_DIR) is a local checkout, not the vendored copy."; \
		   echo "Update it yourself with git; this target only manages deps/conquertron."; exit 1;; \
	esac
	@git -C "$(CONQUERTRON_DIR)" fetch --quiet origin
	@git -C "$(CONQUERTRON_DIR)" checkout --quiet origin/HEAD 2>/dev/null || git -C "$(CONQUERTRON_DIR)" checkout --quiet origin/main
	@c=$$(git -C "$(CONQUERTRON_DIR)" rev-parse HEAD); \
		sed -i "s/^commit .*/commit $$c/" "$(TOPDIR)/../conquertron.lock"; \
		echo "conquertron pinned to $$c"; \
		git -C "$(CONQUERTRON_DIR)" log --oneline -1



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
