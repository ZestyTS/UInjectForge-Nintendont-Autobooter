#include "uif_disc_partition.h"

#include <string.h>
#include <stdlib.h>

#include "common.h"
#include "wdvd.h"

enum
{
	UIF_DI_OPEN_PARTITION = 0x8B,
	UIF_DI_CLOSE_PARTITION = 0x8C,
	UIF_DISC_PARTITION_DIRECTORY_OFFSET = 0x40000,
	UIF_DISC_PARTITION_TYPE_DATA = 0,
	UIF_DISC_MAX_PARTITIONS = 32,
	UIF_DI_COMMAND_SIZE = 0x20,
	UIF_DI_OPTIONAL_TICKET_LENGTH = 0x024A,
	UIF_DI_TMD_OUTPUT_SIZE = 0x49E4,
	UIF_DI_STATUS_OUTPUT_SIZE = 0x20
};

static bool s_partitionOpened = false;

static u32 align32(u32 value)
{
	return (value + 31u) & ~31u;
}

static s32 normalize_ios_result(s32 result)
{
	if(result < 0)
		return result;

	return result == 1 ? 0 : -result;
}

static s32 close_partition(void)
{
	u32 command[8] ATTRIBUTE_ALIGN(32);
	memset(command, 0, sizeof(command));
	command[0] = UIF_DI_CLOSE_PARTITION << 24;
	s_partitionOpened = false;

	return normalize_ios_result(IOS_Ioctl(
		WDVD_GetHandle(),
		UIF_DI_CLOSE_PARTITION,
		command,
		sizeof(command),
		NULL,
		0));
}

static s32 find_data_partition(u64 *partitionOffset)
{
	u32 directory[8] ATTRIBUTE_ALIGN(32);
	memset(directory, 0, sizeof(directory));

	s32 result = WDVD_UnencryptedRead(
		directory,
		sizeof(directory),
		UIF_DISC_PARTITION_DIRECTORY_OFFSET);
	if(result < 0)
		return result;

	u32 partitionCount = directory[0];
	u64 tableOffset = (u64)directory[1] << 2;
	if(partitionCount == 0
		|| partitionCount > UIF_DISC_MAX_PARTITIONS
		|| tableOffset == 0)
	{
		return -1;
	}

	u32 tableBytes = partitionCount * 2u * sizeof(u32);
	u32 readBytes = align32(tableBytes);
	u32 *entries = malloca(readBytes, 32);
	if(!entries)
		return -1;

	memset(entries, 0, readBytes);
	result = WDVD_UnencryptedRead(entries, readBytes, tableOffset);
	if(result == 0)
	{
		result = -1;
		for(u32 index = 0; index < partitionCount; index++)
		{
			u32 wordOffset = entries[index * 2u];
			u32 type = entries[index * 2u + 1u];
			if(type == UIF_DISC_PARTITION_TYPE_DATA && wordOffset != 0)
			{
				*partitionOffset = (u64)wordOffset << 2;
				result = 0;
				break;
			}
		}
	}

	free(entries);
	return result;
}

static s32 open_partition(u64 partitionOffset)
{
	u32 vectorBytes = align32(5u * sizeof(ioctlv));
	u32 tmdBufferBytes = align32(UIF_DI_TMD_OUTPUT_SIZE);
	ioctlv *vectors = malloca(vectorBytes, 32);
	u32 *command = malloca(UIF_DI_COMMAND_SIZE, 32);
	u8 *tmdOutput = malloca(tmdBufferBytes, 32);
	u8 *statusOutput = malloca(UIF_DI_STATUS_OUTPUT_SIZE, 32);

	if(!vectors || !command || !tmdOutput || !statusOutput)
	{
		free(vectors);
		free(command);
		free(tmdOutput);
		free(statusOutput);
		return -1;
	}

	memset(vectors, 0, vectorBytes);
	memset(command, 0, UIF_DI_COMMAND_SIZE);
	memset(tmdOutput, 0, tmdBufferBytes);
	memset(statusOutput, 0, UIF_DI_STATUS_OUTPUT_SIZE);

	command[0] = UIF_DI_OPEN_PARTITION << 24;
	command[1] = (u32)(partitionOffset >> 2);

	vectors[0].data = command;
	vectors[0].len = UIF_DI_COMMAND_SIZE;
	vectors[1].data = NULL;
	vectors[1].len = UIF_DI_OPTIONAL_TICKET_LENGTH;
	vectors[2].data = NULL;
	vectors[2].len = 0;
	vectors[3].data = tmdOutput;
	vectors[3].len = UIF_DI_TMD_OUTPUT_SIZE;
	vectors[4].data = statusOutput;
	vectors[4].len = UIF_DI_STATUS_OUTPUT_SIZE;

	s32 result = IOS_Ioctlv(
		WDVD_GetHandle(),
		UIF_DI_OPEN_PARTITION,
		3,
		2,
		vectors);

	free(vectors);
	free(command);
	free(tmdOutput);
	free(statusOutput);
	return normalize_ios_result(result);
}

s32 UifDiscPartition_OpenData(void)
{
	if(WDVD_GetHandle() < 0)
		return WDVD_GetHandle();

	u32 coverStatus = 0;
	s32 result = WDVD_GetCoverStatus(&coverStatus);
	if(result < 0)
		return result;
	if(coverStatus != 2)
		return -1;

	result = close_partition();
	if(result < 0)
		return result;

	result = WDVD_Reset();
	if(result < 0)
		return result;

	u8 discId[32] ATTRIBUTE_ALIGN(32);
	result = WDVD_ReadDiskId(discId);
	if(result < 0)
		return result;

	u64 partitionOffset = 0;
	result = find_data_partition(&partitionOffset);
	if(result < 0)
		return result;

	result = open_partition(partitionOffset);
	if(result == 0)
		s_partitionOpened = true;

	return result;
}

void UifDiscPartition_CloseIfOpen(void)
{
	if(s_partitionOpened && WDVD_GetHandle() >= 0)
		close_partition();
}
