# SD Game Transfer - Nintendo 2DS/3DS
# Creado por frisk

ifeq ($(strip $(DEVKITARM)),)
$(error Falta DEVKITARM: instala devkitPro o usa su shell)
endif

include $(DEVKITARM)/3ds_rules

TARGET  := sd-game-transfer
BUILD   := build
SOURCES := source
SMDH    := smdh.bin
ICON    := icon.png

INCLUDE := -I$(CURDIR)/$(SOURCES) \
           -I$(DEVKITPRO)/libctru/include

ARCH    := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS  := -g -Wall -O2 -ffunction-sections -fdata-sections \
           $(ARCH) $(INCLUDE)
LDFLAGS := -specs=3dsx.specs -g $(ARCH) -Wl,--gc-sections

LIBDIRS := $(DEVKITPRO)/libctru/lib
LIBS    := -lcitro2d -lcitro3d -lctru -lm

CFILES  := $(wildcard $(SOURCES)/*.c)
OFILES  := $(patsubst $(SOURCES)/%.c,$(BUILD)/%.o,$(CFILES))

.PHONY: all clean

all: $(TARGET).3dsx

$(BUILD):
	@mkdir -p $(BUILD)

$(BUILD)/%.o: $(SOURCES)/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(SMDH): $(ICON)
	smdhtool --create "SD Game Transfer" "Copia archivos y partidas en la SD" "frisk" $< $@

$(TARGET).elf: $(OFILES)
	$(CC) $(OFILES) $(LDFLAGS) $(LIBDIRS:%=-L%) $(LIBS) -o $@

$(TARGET).3dsx: $(TARGET).elf $(SMDH)
	3dsxtool $< $@ --smdh=$(SMDH)

clean:
	@rm -rf $(BUILD) $(TARGET).elf $(TARGET).3dsx $(SMDH)
