#ifndef UIF_NINTENDONT_CONFIG_H
#define UIF_NINTENDONT_CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define UIF_NINTENDONT_CONFIG_SIZE 548u

/*
 * Opaque wire payload passed to Nintendont as argv[1]. UIF owns the matching
 * serializer and only needs the small set of fields exposed by these helpers.
 */
typedef struct
{
	uint8_t bytes[UIF_NINTENDONT_CONFIG_SIZE];
} UifNintendontConfig;

_Static_assert(
	sizeof(UifNintendontConfig) == UIF_NINTENDONT_CONFIG_SIZE,
	"Nintendont config wire payload must remain 548 bytes");

bool UifNintendontConfig_IsValid(const UifNintendontConfig *config);
bool UifNintendontConfig_HasGamePath(
	const UifNintendontConfig *config,
	const char *path);
void UifNintendontConfig_UseInternalDisc(UifNintendontConfig *config);

#endif
