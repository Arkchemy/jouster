#---------------------------------------------------------------------------------
# Bramble's first full-game build: the real, complete recompiled
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

TARGET		:=	Jouster
BUILD		:=	build
SOURCES		:=	source ../../recomp/include
DATA		:=	data
INCLUDES	:=	include ../../recomp/include

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

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
			$(foreach dir,$(DATA),$(CURDIR)/$(dir))

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

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
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
