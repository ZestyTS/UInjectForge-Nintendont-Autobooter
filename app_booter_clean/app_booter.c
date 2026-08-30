/*
 * Clean UIF DOL booter stub.
 *
 * This is intentionally independent from the upstream GPL app_booter component
 * that was present in the source snapshot. It performs only the minimum work
 * needed after the forwarder has shut down IOS and stopped multitasking. It
 * relocates the DOL image already loaded at 0x92000000, flushes caches,
 * and jumps to the DOL entry point.
 */

typedef unsigned int u32;
typedef unsigned char u8;
typedef unsigned long uintptr_t;

#define DOL_IMAGE_ADDR ((const u8 *)0x92000000)
#define CACHE_LINE_SIZE 32u
#define DOL_TEXT_SECTION_COUNT 7
#define DOL_DATA_SECTION_COUNT 11
#define ARGS_ADDR ((const void *)0x93300800)
#define ARGV_STRUCT_SIZE (6u * sizeof(u32))

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

static void *copy_memory(void *destination, const void *source, u32 size)
{
	u8 *dst = (u8 *)destination;
	const u8 *src = (const u8 *)source;

	while(size--)
		*dst++ = *src++;

	return destination;
}

static void sync_range(const void *address, u32 size)
{
	uintptr_t start = (uintptr_t)address & ~(uintptr_t)(CACHE_LINE_SIZE - 1u);
	uintptr_t end = ((uintptr_t)address + size + CACHE_LINE_SIZE - 1u) &
		~(uintptr_t)(CACHE_LINE_SIZE - 1u);

	for(uintptr_t cursor = start; cursor < end; cursor += CACHE_LINE_SIZE)
		__asm__ volatile(
			"dcbst 0,%0; sync; icbi 0,%0"
			:
			: "r"(cursor)
			: "memory");

	__asm__ volatile("sync; isync" : : : "memory");
}

static void relocate_sections(
	const u8 *image,
	const u32 *offsets,
	const u32 *addresses,
	const u32 *sizes,
	u32 count)
{
	for(u32 index = 0; index < count; index++)
	{
		u32 size = sizes[index];
		u32 address = addresses[index];

		if(size == 0 || address == 0)
			continue;

		void *destination = (void *)address;
		copy_memory(destination, image + offsets[index], size);
		sync_range(destination, size);
	}
}

void app_booter_main(void)
{
	const DolHeader *dol = (const DolHeader *)DOL_IMAGE_ADDR;

	relocate_sections(
		DOL_IMAGE_ADDR,
		dol->textOffset,
		dol->textAddress,
		dol->textSize,
		DOL_TEXT_SECTION_COUNT);

	relocate_sections(
		DOL_IMAGE_ADDR,
		dol->dataOffset,
		dol->dataAddress,
		dol->dataSize,
		DOL_DATA_SECTION_COUNT);

	void *argvDestination = (void *)(dol->entryPoint + 8u);
	copy_memory(argvDestination, ARGS_ADDR, ARGV_STRUCT_SIZE);
	sync_range(argvDestination, ARGV_STRUCT_SIZE);

	void (*entry)(void) = (void (*)(void))dol->entryPoint;
	entry();

	for(;;)
		__asm__ volatile("nop");
}
