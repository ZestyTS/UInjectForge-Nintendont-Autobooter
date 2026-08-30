#---------------------------------------------------------------------------------
# Clear the implicit built in rules
#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------
ifeq ($(strip $(DEVKITPPC)),)
$(error "Please set DEVKITPPC in your environment. export DEVKITPPC=<path to>devkitPPC")
endif

include $(DEVKITPPC)/wii_rules

#---------------------------------------------------------------------------------
# TARGET is the name of the output
# BUILD is the directory where object files & intermediate files will be placed
# SOURCES is a list of directories containing source code
# INCLUDES is a list of directories containing extra header files
#---------------------------------------------------------------------------------
SOURCES		:=	source
DATA		:=	data
INCLUDES	:=	source
BOOTER_DIR	:=	app_booter_clean
BOOTER_BUILD	:=	$(BOOTER_DIR)/build
BOOTER_ELF	:=	$(BOOTER_BUILD)/app_booter.elf
BOOTER_BIN	:=	$(BOOTER_BUILD)/app_booter_bin.bin
BOOTER_HEADER	:=	source/app_booter_bin.h
BOOTER_SOURCE	:=	source/app_booter_bin.c
BOOTER_GENERATED_STAMP := $(BOOTER_BUILD)/generated.stamp
GENERATED_CFILES	:=	app_booter_bin.c

TARGET_NORMAL	:=	nintendont_loader
TARGET_DEBUG	:=	nintendont_loader_dbg

BUILD_NORMAL	:=	build
BUILD_DEBUG	:=	build_dbg

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
CFLAGS	= -Ofast -Wall -flto=auto -fno-fat-lto-objects -fuse-linker-plugin

ifeq ($(strip $(DEBUG_BUILD)), 1)
	CFLAGS	+=	-DDEBUG_BUILD
	TARGET	:=	$(TARGET_DEBUG)
	BUILD	:=	$(BUILD_DEBUG)
else
	TARGET	:=	$(TARGET_NORMAL)
	BUILD	:=	$(BUILD_NORMAL)
endif

CFLAGS		+=	$(MACHDEP) $(INCLUDE)
CXXFLAGS	=	$(CFLAGS)
LDFLAGS		=	$(CFLAGS) -Wl,-Map,$(notdir $@).map

#---------------------------------------------------------------------------------
# any extra libraries we wish to link with the project
#---------------------------------------------------------------------------------
LIBS	:= -lfat -lwiiuse -lbte -logc -lm

#---------------------------------------------------------------------------------
# list of directories containing libraries, this must be the top level containing
# include and lib
#---------------------------------------------------------------------------------
LIBDIRS	:= $(DEVKITPPC)/lib $(CURDIR)

#---------------------------------------------------------------------------------
# no real need to edit anything past this point unless you need to add additional
# rules for different file extensions
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/$(TARGET)

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
					$(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

#---------------------------------------------------------------------------------
# automatically build a list of object files for our project
#---------------------------------------------------------------------------------
CFILES		:=	$(filter-out $(GENERATED_CFILES),$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c))))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
sFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.S)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

#---------------------------------------------------------------------------------
# use CXX for linking C++ projects, CC for standard C
#---------------------------------------------------------------------------------
ifeq ($(strip $(CPPFILES)),)
	export LD	:=	$(CC)
else
	export LD	:=	$(CXX)
endif

export OFILES	:=	$(addsuffix .o,$(BINFILES)) \
					$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(GENERATED_CFILES:.c=.o) \
					$(sFILES:.s=.o) $(SFILES:.S=.o)

#---------------------------------------------------------------------------------
# build a list of include paths
#---------------------------------------------------------------------------------
export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
					$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
					-I$(CURDIR)/$(BUILD) \
					-I$(LIBOGC_INC)

#---------------------------------------------------------------------------------
# build a list of library paths
#---------------------------------------------------------------------------------
export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib) \
					-L$(LIBOGC_LIB)

export OUTPUT	:=	$(CURDIR)/$(TARGET)
.PHONY: $(BUILD) clean

#---------------------------------------------------------------------------------
$(BUILD): $(BOOTER_GENERATED_STAMP)
	+@test -s $(BOOTER_HEADER) -a -s $(BOOTER_SOURCE) || { \
		rm -f $(BOOTER_GENERATED_STAMP); \
		$(MAKE) --no-print-directory $(BOOTER_GENERATED_STAMP); \
	}
	@[ -d $@ ] || mkdir -p $@
	+@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

$(BOOTER_GENERATED_STAMP): $(BOOTER_BIN)
	@cp $(BOOTER_BIN) app_booter_bin.bin
	@raw2c app_booter_bin.bin
	@mv app_booter_bin.h $(BOOTER_HEADER)
	@mv app_booter_bin.c $(BOOTER_SOURCE)
	@rm -f app_booter_bin.bin
	@touch $@

$(BOOTER_BIN): $(BOOTER_ELF)
	@$(OBJCOPY) -O binary $< $@

$(BOOTER_ELF): $(BOOTER_DIR)/app_booter_start.S $(BOOTER_DIR)/app_booter.c $(BOOTER_DIR)/link.ld
	@[ -d $(BOOTER_BUILD) ] || mkdir -p $(BOOTER_BUILD)
	@$(CC) $(MACHDEP) -Os -ffreestanding -fno-builtin -fno-exceptions -fno-unwind-tables \
		-fno-asynchronous-unwind-tables -nostdlib -T $(BOOTER_DIR)/link.ld -Wl,--gc-sections \
		-o $@ $(BOOTER_DIR)/app_booter_start.S $(BOOTER_DIR)/app_booter.c -lgcc

#---------------------------------------------------------------------------------
clean:
	@echo clean ...
	@rm -fr $(CURDIR)/$(BUILD_NORMAL) $(CURDIR)/$(TARGET_NORMAL).elf $(CURDIR)/$(TARGET_NORMAL).dol \
		$(CURDIR)/$(BUILD_DEBUG) $(CURDIR)/$(TARGET_DEBUG).elf $(CURDIR)/$(TARGET_DEBUG).dol \
		$(CURDIR)/$(BOOTER_BUILD) $(CURDIR)/$(BOOTER_HEADER) $(CURDIR)/$(BOOTER_SOURCE)


#---------------------------------------------------------------------------------
else

DEPENDS	:=	$(OFILES:.o=.d)

#---------------------------------------------------------------------------------
# main targets
#---------------------------------------------------------------------------------
$(OUTPUT).dol: $(OUTPUT).elf
$(OUTPUT).elf: $(OFILES)

#---------------------------------------------------------------------------------
# This rule links in binary data with the .bin extension
#---------------------------------------------------------------------------------
%.bin.o	:	%.bin
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	$(bin2o)

-include $(DEPENDS)

#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------
