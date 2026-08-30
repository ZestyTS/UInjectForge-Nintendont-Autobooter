#include "uif_nintendont_config.h"

#include <string.h>

enum
{
	UIF_CONFIG_MAGIC_OFFSET = 0,
	UIF_CONFIG_VERSION_OFFSET = 4,
	UIF_CONFIG_FLAGS_OFFSET = 8,
	UIF_CONFIG_GAME_PATH_OFFSET = 20,
	UIF_CONFIG_GAME_PATH_LENGTH = 255,
	UIF_CONFIG_CHEAT_PATH_OFFSET = 275,
	UIF_CONFIG_CHEAT_PATH_LENGTH = 255,
	UIF_CONFIG_MAX_PADS_OFFSET = 532
};

static const uint32_t UIF_CONFIG_MAGIC = 0x01070CF6u;
static const uint32_t UIF_CONFIG_VERSION = 10u;
static const uint32_t UIF_CONFIG_AUTOBOOT_MASK = 1u << 7;
static const uint32_t UIF_CONFIG_USB_MASK = 1u << 10;

static uint32_t read_be32(const uint8_t *bytes)
{
	return ((uint32_t)bytes[0] << 24)
		| ((uint32_t)bytes[1] << 16)
		| ((uint32_t)bytes[2] << 8)
		| (uint32_t)bytes[3];
}

static void write_be32(uint8_t *bytes, uint32_t value)
{
	bytes[0] = (uint8_t)(value >> 24);
	bytes[1] = (uint8_t)(value >> 16);
	bytes[2] = (uint8_t)(value >> 8);
	bytes[3] = (uint8_t)value;
}

bool UifNintendontConfig_IsValid(const UifNintendontConfig *config)
{
	if(!config)
		return false;

	return read_be32(config->bytes + UIF_CONFIG_MAGIC_OFFSET) == UIF_CONFIG_MAGIC
		&& read_be32(config->bytes + UIF_CONFIG_VERSION_OFFSET) == UIF_CONFIG_VERSION
		&& config->bytes[UIF_CONFIG_MAX_PADS_OFFSET] <= 4u;
}

bool UifNintendontConfig_HasGamePath(
	const UifNintendontConfig *config,
	const char *path)
{
	if(!config || !path)
		return false;

	size_t pathLength = strlen(path);
	if(pathLength >= UIF_CONFIG_GAME_PATH_LENGTH)
		return false;

	const uint8_t *gamePath = config->bytes + UIF_CONFIG_GAME_PATH_OFFSET;
	return memcmp(gamePath, path, pathLength) == 0 && gamePath[pathLength] == 0;
}

void UifNintendontConfig_UseInternalDisc(UifNintendontConfig *config)
{
	if(!config)
		return;

	uint8_t *gamePath = config->bytes + UIF_CONFIG_GAME_PATH_OFFSET;
	uint32_t flags = read_be32(config->bytes + UIF_CONFIG_FLAGS_OFFSET);

	memset(gamePath, 0, UIF_CONFIG_GAME_PATH_LENGTH);
	memset(
		config->bytes + UIF_CONFIG_CHEAT_PATH_OFFSET,
		0,
		UIF_CONFIG_CHEAT_PATH_LENGTH);
	memcpy(gamePath, "di", sizeof("di"));

	flags |= UIF_CONFIG_AUTOBOOT_MASK;
	flags &= ~UIF_CONFIG_USB_MASK;
	write_be32(config->bytes + UIF_CONFIG_FLAGS_OFFSET, flags);
}
