/*
 * Copyright (C) 2017 FIX94
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#include <gccore.h>
#include <stdio.h>
#include <fat.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdint.h>
#include <ogc/lwp_threads.h>
#include <ogc/pad.h>
#include <sdcard/wiisd_io.h>
#include <wiiuse/wpad.h>
#include "app_booter_bin.h"
#include "uif_disc_partition.h"
#include "uif_nintendont_config.h"
#include "wdvd.h"

#define MAX_NINTENDONT_DOL_SIZE (0x92F00000u - 0x92000000u)
#define MAX_BOOTER_SIZE (0x93300800u - 0x92F00000u)
#define AUTO_RETRY_LIMIT 2
#define AUTO_RETRY_DELAY_SECONDS 1
#define RECOVERY_TIMEOUT_FRAMES (60 * 30)
#define DOL_TEXT_SECTION_COUNT 7
#define DOL_DATA_SECTION_COUNT 11
#define DEBUG_LOG_PATH "sd:/uinjectforge-nintendont-boot.log"
#define DEBUG_LOG_FALLBACK_PATH "sd:/apps/nintendont/uinjectforge-nintendont-boot.log"
#define DEBUG_LOG_HEADER "UInjectForge Nintendont Autobooter debug log"
#define UIF_NO_AUTOBOOT_GAME_PATH "uif:no-autoboot"
#define WII_MEM1_START 0x80000000u
#define WII_MEM1_END 0x81800000u
#define WII_MEM2_START 0x90000000u
#define WII_MEM2_END 0x94000000u
#define LOADER_PROTECTED_START 0x92000000u
#define LOADER_PROTECTED_END 0x93301000u
#define ARGV_COPY_SIZE (6u * sizeof(u32))

static u8 *EXECUTE_ADDR = (u8*)0x92000000;
static u8 *BOOTER_ADDR = (u8*)0x92F00000;
static void (*entry)() = (void*)0x92F00000;
static struct __argv *ARGS_ADDR = (struct __argv*)0x93300800;

static void *xfb = NULL;

static bool statusLogEnabled = false;
static bool statusLogInitialized = false;
static const char *statusLogPath = DEBUG_LOG_PATH;

static void statusLogWrite(const char *text)
{
	if(!statusLogEnabled)
		return;

	FILE *log = fopen(statusLogPath, "ab");
	if(log)
	{
		fputs(text, log);
		fclose(log);
	}
}

static void statusLogEnable()
{
	statusLogEnabled = true;
	statusLogPath = DEBUG_LOG_PATH;
	FILE *log = fopen(statusLogPath, statusLogInitialized ? "ab" : "wb");
	if(!log)
	{
		statusLogPath = DEBUG_LOG_FALLBACK_PATH;
		log = fopen(statusLogPath, statusLogInitialized ? "ab" : "wb");
	}
	if(log)
	{
		if(!statusLogInitialized)
			fprintf(log, "%s\n", DEBUG_LOG_HEADER);
		fclose(log);
		statusLogInitialized = true;
	}
}

static void statusLogDisable()
{
	statusLogEnabled = false;
}

typedef enum RecoveryAction
{
	RECOVERY_RETRY,
	RECOVERY_LAUNCH_NINTENDONT,
	RECOVERY_RETURN_MENU
} RecoveryAction;

typedef struct DolHeader
{
	u32 textOffset[DOL_TEXT_SECTION_COUNT];
	u32 dataOffset[DOL_DATA_SECTION_COUNT];
	u32 textAddress[DOL_TEXT_SECTION_COUNT];
	u32 dataAddress[DOL_DATA_SECTION_COUNT];
	u32 textSize[DOL_TEXT_SECTION_COUNT];
	u32 dataSize[DOL_DATA_SECTION_COUNT];
	u32 bssAddress;
	u32 bssSize;
	u32 entryPoint;
} DolHeader;

typedef struct DolMemoryRange
{
	u32 address;
	u32 size;
	bool isText;
} DolMemoryRange;

static void initGraphics()
{
	if(xfb)
		return;

	VIDEO_Init();

	GXRModeObj *rmode = VIDEO_GetPreferredMode(NULL);
	if(!rmode)
		return;

	xfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
	if(!xfb)
		return;

	VIDEO_Configure(rmode);
	VIDEO_SetNextFramebuffer(xfb);
	VIDEO_SetBlack(FALSE);
	VIDEO_Flush();
	VIDEO_WaitVSync();
	if(rmode->viTVMode&VI_NON_INTERLACE)
		VIDEO_WaitVSync();

	CON_InitEx(rmode, 24, 32, rmode->fbWidth - 32, rmode->xfbHeight - 48);
	VIDEO_ClearFrameBuffer(rmode, xfb, COLOR_BLACK);
	printf(" \n");
}

#ifdef DEBUG_BUILD
static void debugWriteRaw(const char *text)
{
	initGraphics();
	printf("%s", text);

	statusLogWrite(text);
}

static void debugPrint(const char *msg, ...)
{
	char buffer[512];
	va_list args;
	va_start(args, msg);
	vsnprintf(buffer, sizeof(buffer), msg, args);
	va_end(args);
	debugWriteRaw(buffer);
}

static void debugStatus(const char *code, const char *msg, ...)
{
	char body[384];
	char line[512];
	va_list args;
	va_start(args, msg);
	vsnprintf(body, sizeof(body), msg, args);
	va_end(args);
	snprintf(line, sizeof(line), "[UIF-%s] %s\n", code, body);
	debugWriteRaw(line);
}

static void debugEnableSdLog()
{
	statusLogEnable();
	debugStatus("S01", "SD mounted; log path %s", statusLogPath);
}

static void debugDisableSdLog()
{
	statusLogDisable();
}
	#define nPrintf(x) debugPrint(x)
	#undef NO_DISPLAY
#else
	#define debugPrint(...)
static void debugStatus(const char *code, const char *msg, ...)
{
	char body[384];
	char line[512];
	va_list args;
	va_start(args, msg);
	vsnprintf(body, sizeof(body), msg, args);
	va_end(args);
	snprintf(line, sizeof(line), "[UIF-%s] %s\n", code, body);
	statusLogWrite(line);
}

static void debugEnableSdLog()
{
	statusLogEnable();
	debugStatus("S01", "SD mounted; log path %s", statusLogPath);
}

static void debugDisableSdLog()
{
	statusLogDisable();
}

static inline void nPrintf(const char *msg, ...)
{
	initGraphics();
	va_list args;
	va_start(args, msg);
	vprintf(msg, args);
	va_end(args);
	sleep(2);
}
#endif

static bool mountSD()
{
	__io_wiisd.startup();
	__io_wiisd.isInserted();
	return fatMount("sd", &__io_wiisd, 0, 4, 64);
}

static inline void unmountSD()
{
	fatUnmount("sd:");
	__io_wiisd.shutdown();
	debugDisableSdLog();
}

static inline void unmountISO()
{
	WDVD_FST_Unmount();
	WDVD_Close();
}

static uint32_t getIdFromIso()
{
	if(WDVD_FST_OpenDisc(0) == 0)
	{
		uint32_t ret;
		if(WDVD_FST_Read((uint8_t *)&ret, 4) == 4)
		{
			WDVD_FST_Close();
			return ret;
		}

		WDVD_FST_Close();
		debugPrint("Error reading iso!\n");
	}
	else
		debugPrint("Error opening iso!\n");

	return 0;
}

static void showWiiVcDiscError()
{
	nPrintf("Unable to read Wii VC game.iso!\n"
		"Check the GCN wrapper image and rebuild the inject.\n");
}

static bool shouldAutoRetry(int *retryCount)
{
	if(*retryCount >= AUTO_RETRY_LIMIT)
		return false;

	(*retryCount)++;
	sleep(AUTO_RETRY_DELAY_SECONDS);
	return true;
}

static RecoveryAction waitForRecoveryAction(const char *message, bool canLaunchNintendont)
{
	initGraphics();
	printf("\x1b[2J\x1b[0;0H");
	printf("%s\n\n", message);
	printf("A: Retry\n");
	if(canLaunchNintendont)
		printf("1/+/X: Open Nintendont without autoboot\n");
	printf("B/HOME: Return to Wii U Menu\n");
	printf("No input: return to Wii U Menu\n");

	WPAD_Init();
	PAD_Init();
	for(int frame = 0; frame < RECOVERY_TIMEOUT_FRAMES; frame++)
	{
		VIDEO_WaitVSync();
		WPAD_ScanPads();
		PAD_ScanPads();

		u32 wpadDown = WPAD_ButtonsDown(0);
		u32 padDown = PAD_ButtonsDown(0);

		if((wpadDown & WPAD_BUTTON_A) || (padDown & PAD_BUTTON_A))
			return RECOVERY_RETRY;

		if(canLaunchNintendont
			&& ((wpadDown & (WPAD_BUTTON_1 | WPAD_BUTTON_PLUS)) || (padDown & PAD_BUTTON_X)))
			return RECOVERY_LAUNCH_NINTENDONT;

		if((wpadDown & (WPAD_BUTTON_B | WPAD_BUTTON_HOME)) || (padDown & (PAD_BUTTON_B | PAD_BUTTON_START)))
			return RECOVERY_RETURN_MENU;
	}

	return RECOVERY_RETURN_MENU;
}

static RecoveryAction recoverFromError(const char *message, bool canLaunchNintendont, int *retryCount)
{
	if(shouldAutoRetry(retryCount))
		return RECOVERY_RETRY;

	*retryCount = 0;
	return waitForRecoveryAction(message, canLaunchNintendont);
}

static void returnToMenu()
{
	unmountISO();
	unmountSD();
	SYS_ResetSystem(SYS_RETURNTOMENU,0,0);
	for(;;)
		VIDEO_WaitVSync();
}

static void launchNintendont(const char *fPath, const UifNintendontConfig *nincfg)
{
	char *CMD_ADDR = (char*)ARGS_ADDR + sizeof(struct __argv);
	size_t argsSize;
	size_t fullArgsSize;

	if(nincfg)
	{
		size_t pathSize = strlen(fPath) + 1;
		argsSize = pathSize + sizeof(UifNintendontConfig) + 1;
		fullArgsSize = sizeof(struct __argv) + argsSize;
		memset(ARGS_ADDR, 0, fullArgsSize);
		ARGS_ADDR->argvMagic = ARGV_MAGIC;
		ARGS_ADDR->commandLine = CMD_ADDR;
		ARGS_ADDR->length = argsSize;
		ARGS_ADDR->argc = 2;
		memcpy(CMD_ADDR, fPath, pathSize);
		memcpy(CMD_ADDR + pathSize, nincfg, sizeof(UifNintendontConfig));
		CMD_ADDR[pathSize + sizeof(UifNintendontConfig)] = 0;
		DCFlushRange(ARGS_ADDR, fullArgsSize);
	}
	else
	{
		size_t pathSize = strlen(fPath) + 1;
		argsSize = pathSize;
		fullArgsSize = sizeof(struct __argv) + argsSize;
		memset(ARGS_ADDR, 0, fullArgsSize);
		ARGS_ADDR->argvMagic = ARGV_MAGIC;
		ARGS_ADDR->commandLine = CMD_ADDR;
		ARGS_ADDR->length = argsSize;
		ARGS_ADDR->argc = 1;
		memcpy(CMD_ADDR, fPath, pathSize);
		DCFlushRange(ARGS_ADDR, fullArgsSize);
	}

	SYS_ResetSystem(SYS_SHUTDOWN,0,0);
	__lwp_thread_stopmultitasking(entry);
}

static FILE *openNintendontDol(const char **resolvedPath)
{
	static const char *paths[] = {
		"sd:/apps/nintendont/boot.dol",
		"sd:/apps/Nintendont/boot.dol"
	};

	for(size_t index = 0; index < sizeof(paths) / sizeof(paths[0]); index++)
	{
		FILE *file = fopen(paths[index],"rb");
		if(file)
		{
			*resolvedPath = paths[index];
			return file;
		}
	}

	*resolvedPath = paths[0];
	return NULL;
}

static bool tryGetRangeEnd(u32 address, u32 size, u32 *end)
{
	if(size == 0 || address > 0xFFFFFFFFu - size)
		return false;

	*end = address + size;
	return *end > address;
}

static bool rangesOverlap(u32 firstAddress, u32 firstSize, u32 secondAddress, u32 secondSize)
{
	u32 firstEnd = 0;
	u32 secondEnd = 0;
	if(!tryGetRangeEnd(firstAddress, firstSize, &firstEnd)
		|| !tryGetRangeEnd(secondAddress, secondSize, &secondEnd))
		return true;

	return firstAddress < secondEnd && secondAddress < firstEnd;
}

static bool isAllowedLoadRange(u32 address, u32 size)
{
	u32 end = 0;
	if(!tryGetRangeEnd(address, size, &end))
		return false;

	bool inWiiMemory =
		(address >= WII_MEM1_START && end <= WII_MEM1_END)
		|| (address >= WII_MEM2_START && end <= WII_MEM2_END);
	if(!inWiiMemory)
		return false;

	return !rangesOverlap(
		address,
		size,
		LOADER_PROTECTED_START,
		LOADER_PROTECTED_END - LOADER_PROTECTED_START);
}

static bool isDolSectionValid(u32 offset, u32 address, u32 size, size_t imageSize)
{
	if(size == 0)
		return true;

	if(address == 0
		|| offset < sizeof(DolHeader)
		|| (size_t)offset > imageSize
		|| (size_t)size > imageSize - (size_t)offset
		|| !isAllowedLoadRange(address, size))
		return false;

	return true;
}

static bool isValidDolImage(const void *image, size_t imageSize)
{
	if(!image || imageSize < sizeof(DolHeader))
		return false;

	const DolHeader *dol = (const DolHeader *)image;
	if(dol->entryPoint == 0 || (dol->entryPoint & 3u) != 0)
		return false;

	DolMemoryRange ranges[DOL_TEXT_SECTION_COUNT + DOL_DATA_SECTION_COUNT];
	u32 rangeCount = 0;
	for(u32 index = 0; index < DOL_TEXT_SECTION_COUNT; index++)
	{
		if(!isDolSectionValid(dol->textOffset[index], dol->textAddress[index], dol->textSize[index], imageSize))
			return false;
		if(dol->textSize[index] != 0)
		{
			ranges[rangeCount].address = dol->textAddress[index];
			ranges[rangeCount].size = dol->textSize[index];
			ranges[rangeCount].isText = true;
			rangeCount++;
		}
	}

	for(u32 index = 0; index < DOL_DATA_SECTION_COUNT; index++)
	{
		if(!isDolSectionValid(dol->dataOffset[index], dol->dataAddress[index], dol->dataSize[index], imageSize))
			return false;
		if(dol->dataSize[index] != 0)
		{
			ranges[rangeCount].address = dol->dataAddress[index];
			ranges[rangeCount].size = dol->dataSize[index];
			ranges[rangeCount].isText = false;
			rangeCount++;
		}
	}

	if(rangeCount == 0)
		return false;

	for(u32 first = 0; first < rangeCount; first++)
	{
		for(u32 second = first + 1; second < rangeCount; second++)
		{
			if(rangesOverlap(
				ranges[first].address,
				ranges[first].size,
				ranges[second].address,
				ranges[second].size))
				return false;
		}
	}

	if((dol->bssAddress == 0) != (dol->bssSize == 0))
		return false;

	if(dol->bssSize != 0)
	{
		if(!isAllowedLoadRange(dol->bssAddress, dol->bssSize))
			return false;

		for(u32 index = 0; index < rangeCount; index++)
		{
			if(rangesOverlap(
				dol->bssAddress,
				dol->bssSize,
				ranges[index].address,
				ranges[index].size))
				return false;
		}
	}

	for(u32 index = 0; index < rangeCount; index++)
	{
		if(!ranges[index].isText)
			continue;

		u32 textEnd = 0;
		if(!tryGetRangeEnd(ranges[index].address, ranges[index].size, &textEnd))
			return false;

		if(dol->entryPoint >= ranges[index].address && dol->entryPoint < textEnd)
		{
			u32 argvEnd = 0;
			if(!tryGetRangeEnd(dol->entryPoint, 8u + ARGV_COPY_SIZE, &argvEnd))
				return false;

			return argvEnd <= textEnd;
		}
	}

	return false;
}

static bool isUifNoAutobootConfig(const UifNintendontConfig *nincfg)
{
	return UifNintendontConfig_HasGamePath(
		nincfg,
		UIF_NO_AUTOBOOT_GAME_PATH);
}

static bool prepareNintendontLoader(const char **resolvedPath)
{
	FILE *f = openNintendontDol(resolvedPath);
	if(!f)
	{
		debugStatus("E02", "Nintendont boot.dol was not found at sd:/apps/nintendont/boot.dol or sd:/apps/Nintendont/boot.dol");
		return false;
	}

	if(fseek(f,0,SEEK_END) != 0)
	{
		fclose(f);
		debugStatus("E03", "Error seeking Nintendont boot.dol at %s", *resolvedPath);
		return false;
	}

	long rawSize = ftell(f);
	if(rawSize <= 0 || (size_t)rawSize > MAX_NINTENDONT_DOL_SIZE)
	{
		fclose(f);
		debugStatus("E04", "Invalid Nintendont boot.dol size: %ld", rawSize);
		return false;
	}

	size_t fsize = (size_t)rawSize;
	if(fseek(f,0,SEEK_SET) != 0 || fread(EXECUTE_ADDR,1,fsize,f) != fsize)
	{
		fclose(f);
		debugStatus("E05", "Error reading Nintendont boot.dol at %s", *resolvedPath);
		return false;
	}
	fclose(f);

	if(!isValidDolImage(EXECUTE_ADDR,fsize))
	{
		debugStatus("E06", "Invalid Nintendont boot.dol header");
		return false;
	}

	if((size_t)app_booter_bin_size > MAX_BOOTER_SIZE)
	{
		debugStatus("E07", "Invalid UIF app booter size: %u", (unsigned)app_booter_bin_size);
		return false;
	}

	DCFlushRange(EXECUTE_ADDR,fsize);
	debugStatus("S02", "Loaded Nintendont boot.dol from %s (%u bytes)", *resolvedPath, (unsigned)fsize);

	memcpy(BOOTER_ADDR,app_booter_bin,app_booter_bin_size);
	DCFlushRange(BOOTER_ADDR,app_booter_bin_size);
	ICInvalidateRange(BOOTER_ADDR,app_booter_bin_size);
	debugStatus("S03", "UIF app booter copied (%u bytes)", (unsigned)app_booter_bin_size);
	return true;
}

static void launchNintendontWithoutAutoboot(const char **resolvedPath)
{
	if(!mountSD())
	{
		__io_wiisd.shutdown();
		debugStatus("E17", "Unable to remount SD for manual Nintendont launch");
		returnToMenu();
		return;
	}

	debugEnableSdLog();
	if(!prepareNintendontLoader(resolvedPath))
	{
		debugStatus("E18", "Unable to prepare manual Nintendont launch");
		unmountSD();
		returnToMenu();
		return;
	}

	debugStatus("S91", "Launching Nintendont without autoboot configuration");
	unmountSD();
	launchNintendont(*resolvedPath, NULL);
}

int main(int argc, char *argv[])
{
	VIDEO_Init();
	int retryCount = 0;
	const char *fPath = "sd:/apps/nintendont/boot.dol";

retryStartup:
	debugStatus("S00", "Startup attempt %d", retryCount + 1);
	if(!mountSD())
	{
		__io_wiisd.shutdown();
		debugStatus("E01", "Error mounting SD");
		RecoveryAction action = recoverFromError("Error mounting SD.", false, &retryCount);
		if(action == RECOVERY_RETRY)
			goto retryStartup;

		returnToMenu();
		return -1;
	}
	debugEnableSdLog();

	FILE *f = NULL;

	UifNintendontConfig nincfg ATTRIBUTE_ALIGN(32);
	memset(&nincfg, 0, sizeof(nincfg));
	bool launchMenuOnly = false;

	uint32_t gameId = 0;
	debugStatus("S04", "Opening Wii VC disc interface");
	if(WDVD_Init() == 0)
	{
		/*
		 * Wii VC normally exposes the mapped DATA partition directly. This is
		 * the launch path used by the known-working forwarder. Some wrappers
		 * instead require an explicit partition open, so retain that as a
		 * fallback rather than forcing it for every title.
		 */
		bool fstMounted = WDVD_FST_Mount();
		if(fstMounted)
		{
			debugStatus("S05", "Wii VC FST mounted through the direct mapped-partition path");
		}
		else
		{
			debugStatus("W01", "Direct Wii VC FST mount failed; trying explicit DATA partition open");
			s32 partitionResult = UifDiscPartition_OpenData();
			if(partitionResult != 0)
				debugStatus("E08", "Error opening Wii VC data partition: %d", partitionResult);
			else
			{
				fstMounted = WDVD_FST_Mount();
				if(fstMounted)
					debugStatus("S05", "Wii VC FST mounted after explicit DATA partition open");
				else
					debugStatus("E09", "Error mounting Wii VC FST after explicit DATA partition open");
			}
		}

		if(fstMounted)
		{
			gameId = getIdFromIso();
			if(gameId)
			{
				char gameIdText[5];
				memcpy(gameIdText, &gameId, 4);
				gameIdText[4] = 0;
				debugStatus("S06", "Internal disc Game ID %.4s", gameIdText);
			}
			if(!gameId)
				WDVD_FST_Unmount();
		}
		else
			WDVD_Close();
	}
	else
		debugStatus("E10", "Error initialising WDVD");

	if(!gameId)
	{
		showWiiVcDiscError();
		unmountISO();
		unmountSD();
		RecoveryAction action = recoverFromError("Unable to read Wii VC game.iso.", true, &retryCount);
		if(action == RECOVERY_RETRY)
			goto retryStartup;
		if(action == RECOVERY_LAUNCH_NINTENDONT)
			launchNintendontWithoutAutoboot(&fPath);

		returnToMenu();
		return -2;
	}

	char fPath2[] = "sd:/nintendont/configs/XXXX.bin";
	const size_t configPrefixSize = strlen("sd:/nintendont/configs/");
	memcpy(fPath2 + configPrefixSize, &gameId, sizeof(gameId));
	f = fopen(fPath2,"rb");
	if(f)
	{
		if(fread(&nincfg,sizeof(nincfg),1,f) == 1
			&& UifNintendontConfig_IsValid(&nincfg))
		{
			debugStatus("S07", "Game-specific config loaded from %s", fPath2);
			goto clearNincfg;
		}

		debugStatus("E11", "Invalid game-specific config at %s", fPath2);
		fclose(f);
	}
	else
		debugStatus("S08", "No game-specific config at %s", fPath2);

	bool foundConfig = false;
	if(WDVD_FST_Open("nincfg.bin") == 0)
	{
		if(WDVD_FST_Read((uint8_t *)&nincfg,sizeof(nincfg)) == sizeof(nincfg)
			&& UifNintendontConfig_IsValid(&nincfg))
		{
			debugStatus("S09", "Embedded nincfg.bin loaded from Wii VC wrapper");
			foundConfig = true;
		}
		else
			debugStatus("E14", "Invalid embedded nincfg.bin from Wii VC wrapper");

		WDVD_FST_Close();

		if(foundConfig)
		{
			if(isUifNoAutobootConfig(&nincfg))
			{
				launchMenuOnly = true;
				debugStatus("S13", "UIF menu-only profile marker found in embedded config");
			}
			else
			{
				UifNintendontConfig_UseInternalDisc(&nincfg);
				debugStatus("S12", "Forced Nintendont config to Wii VC internal-disc autoboot");
			}
		}
	}
	else
		debugStatus("S10", "No embedded nincfg.bin in Wii VC wrapper");

	if(!foundConfig)
	{
		f = fopen("sd:/nincfg.bin","rb");
		if(!f)
		{
			debugStatus("E15", "No valid nincfg.bin was found");
			unmountISO();
			unmountSD();
			RecoveryAction action = recoverFromError("No valid nincfg.bin was found.", true, &retryCount);
			if(action == RECOVERY_RETRY)
				goto retryStartup;
			if(action == RECOVERY_LAUNCH_NINTENDONT)
				launchNintendontWithoutAutoboot(&fPath);

			returnToMenu();
			return -2;
		}

		if(fread(&nincfg,sizeof(nincfg),1,f) != 1
			|| !UifNintendontConfig_IsValid(&nincfg))
		{
			fclose(f);
			debugStatus("E16", "Invalid fallback nincfg.bin at sd:/nincfg.bin");
			unmountISO();
			unmountSD();
			RecoveryAction action = recoverFromError("Invalid nincfg.bin.", true, &retryCount);
			if(action == RECOVERY_RETRY)
				goto retryStartup;
			if(action == RECOVERY_LAUNCH_NINTENDONT)
				launchNintendontWithoutAutoboot(&fPath);

			returnToMenu();
			return -2;
		}

		debugStatus("S11", "Fallback nincfg.bin loaded from sd:/nincfg.bin");

	clearNincfg:
		fclose(f);
		if(isUifNoAutobootConfig(&nincfg))
		{
			launchMenuOnly = true;
			debugStatus("S13", "UIF menu-only profile marker found in SD config");
		}
		else
		{
			UifNintendontConfig_UseInternalDisc(&nincfg);
			debugStatus("S12", "Forced Nintendont config to Wii VC internal-disc autoboot");
		}
	}

	unmountISO();
	if(!prepareNintendontLoader(&fPath))
	{
		unmountSD();
		RecoveryAction action = recoverFromError("Unable to prepare Nintendont loader.", false, &retryCount);
		if(action == RECOVERY_RETRY)
			goto retryStartup;

		returnToMenu();
		return -1;
	}

	debugStatus(
		launchMenuOnly ? "S91" : "S90",
		launchMenuOnly
			? "Launching Nintendont menu without autoboot configuration"
			: "Launching Nintendont through UIF app booter");
	unmountSD();

	launchNintendont(fPath, launchMenuOnly ? NULL : &nincfg);

	return 0;
}
