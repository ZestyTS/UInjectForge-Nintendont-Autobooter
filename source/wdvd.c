#include "debug.h"
#include "Config.h"
#include "common.h"
#include "string.h"
#include <stdint.h>
#include "alloc.h"
#include "uif_disc_partition.h"
#include "wdvd.h"

#define SECTOR_SIZE 0x800
#define MAX_FST_SIZE (16u * 1024u * 1024u)

typedef struct {
	u32 dol_offset;
	u32 fst_offset;
	u32 fst_size;
	u32 fst_size2;
} __attribute__ ((packed)) FST_INFO;

typedef struct {
	u8 filetype;
	char name_offset[3];
	u32 fileoffset;
	u32 filelen;
} __attribute__((packed)) FST_ENTRY;

typedef struct
{
	bool inUse;
	u32 offset;
	s32 entry;
} OpenFstItem;

//these are the only memory consumed by this devotab
OpenFstItem openDir;
OpenFstItem openFile;
static u8 *fst_buffer = NULL;
static u32 fst_buffer_size = 0;
static u32 fst_entry_count = 0;

//pointer to the name list at the end of fst_buffer
static char *name_table = NULL;
static u32 name_table_size = 0;
static FST_ENTRY *fst = NULL;

static bool is_aligned_32(const void *ptr)
{
	return (((uintptr_t)ptr) & 31u) == 0;
}

static u32 align32_size(u32 len)
{
	return (len + 31u) & ~31u;
}

const char *FstName( const FST_ENTRY *entry )
{
	if( !fst || !entry || entry == &fst[ 0 ] )
	return NULL;

	if( entry < fst || entry >= (fst + fst_entry_count) || !name_table )
		return NULL;

	u32 name_offset =
		((u32)(u8)entry->name_offset[0] << 16) |
		((u32)(u8)entry->name_offset[1] << 8) |
		(u32)(u8)entry->name_offset[2];

	if( name_offset >= name_table_size )
		return NULL;

	const char *name = name_table + name_offset;
	if( !memchr( name, 0, name_table_size - name_offset ) )
		return NULL;

	return name;
}

static int strcmp_slash( const char *s1, const char *s2 )
{
	while( *s1 == *s2++ )
	if( *s1++ == 0 || *s1 == '/' )
		return( 0 );

	return( *(const unsigned char*)s1 - *(const unsigned char *)( s2 - 1 ) );
}

static int strlen_slash( const char *s )
{
	int ret = 0;
	while( *s && *s++ != '/' )
	ret++;

	return ret;
}

static u32 NextEntryInFolder( u32 current, u32 directory )
{
	if( !fst || current >= fst_entry_count || directory >= fst_entry_count )
		return 0;

	u32 next = ( fst[ current ].filetype ? fst[ current ].filelen : current + 1 );
	if( next < fst[ directory ].filelen && next < fst_entry_count )
	return next;

	return 0;
}

static s32 EntryFromPath( const char *path, int d )
{
	if( !fst || !path || d < 0 || (u32)d >= fst_entry_count )
		return -1;

	if( !fst[ d ].filetype )
	{
		//gprintf("ERROR!!  %s is not a directory\n", FstName( &fst[ d ] ) );
		return -1;
	}

	u32 next = d + 1;

	FST_ENTRY *entry = &fst[ next ];

	while( next )
	{
		const char *entryName = FstName( entry );
		//gprintf( "\tcomparing: [ %u ] %s\n", next, FstName( entry ) );
		//does this entry match.
		//strlen_slash is used because if looking for "dvd:/gameboy/" it would return a false positive if it hit "dvd:/gameboy advance/" first
		if( entryName && !strcmp_slash( path, entryName ) && ( strlen( entryName ) == strlen_slash( path ) ) )
		{
			char *slash = strchr( path, '/' );
			if( slash && strlen( slash + 1 ) )
			{
			//we are looking for a file somewhere in this folder
			return EntryFromPath( slash + 1, next );
			}
			//this is the actual entry we were looking for
			return next;
		}

		//find the next entry in this folder
		next = NextEntryInFolder( next, d );
		entry = &fst[ next ];
	}

	//no entry with the given path was found
	return -1;
}

static bool validate_fst_entries(void)
{
	if( !fst || fst_entry_count == 0 )
		return false;

	for( u32 index = 1; index < fst_entry_count; index++ )
	{
		if( !FstName( &fst[ index ] ) )
			return false;

		if( fst[ index ].filetype )
		{
			if( fst[ index ].filelen <= index || fst[ index ].filelen > fst_entry_count )
				return false;
		}
	}

	return true;
}

int WDVD_FST_Open( const char *path )
{
	if( openFile.inUse )
	{
		BootStatusError(-2, -10);
		mdelay(5000);
		//gprintf("tried to open more than 1 file at a time\n");
		return -1;
	}
	openFile.entry = EntryFromPath( path, 0 );
	if( openFile.entry < 0 )
	{
		BootStatusError(-2, -11);
		mdelay(5000);
		//gprintf("invalid filename: %s\n", path );
		return -1;
	}
	if( fst[ openFile.entry ].filetype )
	{
		BootStatusError(-2, -12);
		mdelay(5000);
		return -1;
	}
	openFile.offset = 0;
	openFile.inUse = true;

	return 0;
}

int WDVD_FST_Close()
{
	//gprintf("close\n");
	if( !openFile.inUse )
	{
		//gprintf("tried to close a file that isnt open\n" );
		return -1;
	}
	memset( &openFile, 0, sizeof( OpenFstItem ) );
	return 0;
}

static char *discName[2] = { "game.iso", "disc2.iso" };
int WDVD_FST_OpenDisc(u32 discNum)
{
	if(discNum > 1) return -1;
	return WDVD_FST_Open(discName[discNum]);
}

u32 WDVD_FST_LSeek( u32 pos )
{
	if( pos > fst[ openFile.entry ].filelen )
	{
		//gprintf("seek: shit\n");
		pos = fst[ openFile.entry ].filelen;
	}
	openFile.offset = pos;

	return pos;
}

int WDVD_FST_Read(u8 *ptr, s32 len)
{
	//gprintf("read( %d )\n", fd );
	if( !openFile.inUse )
	{
		//gprintf("read: !openFile.inUse\n");
		return -1;
	}
	if( openFile.offset >= fst[ openFile.entry ].filelen )
	{
		//gprintf("read: 2\n");
		return 0;
	}
	if( len + openFile.offset > fst[ openFile.entry ].filelen )
	{
		//gprintf("read: 3\n");
		len = fst[ openFile.entry ].filelen - openFile.offset;
	}
	if( len <= 0 )
	{
		return 0;
	}

	if( WDVD_Read( ptr, len, (u64)( ( (u64)fst[ openFile.entry ].fileoffset << 2LL) + openFile.offset ) ) < 0 )
	{
		//gprintf("read: failed\n");
		return -1;
	}

	openFile.offset += len;

	return len;
}

static bool read_disc() {
	//gprintf("read_disc()\n");

	s32 ret;
	FST_INFO fst_info;

	//find FST inside partition
	ret = WDVD_Read( (u8*)&fst_info, sizeof( FST_INFO ), 0x420LL );
	if( ret < 0 )
	{
		BootStatusError(-2, -8);
		mdelay(5000);
		//gprintf("WDVD_Read( fst_info ): %d\n", ret );
		goto error;
	}
	fst_info.fst_offset <<= 2;
	fst_info.fst_size <<= 2;
	//gprintf("fst offset: %08x\tsize: %08x\n", fst_info.fst_offset, fst_info.fst_size );
	//hexdump( &fst_info, 0x20 );

	if( fst_info.fst_size == 0 || fst_info.fst_size > MAX_FST_SIZE )
		goto error;

	fst_buffer = (u8*)malloca( fst_info.fst_size, 32 );
	if( !fst_buffer ) goto error;

	//read fst into memory
	ret = WDVD_Read( fst_buffer, fst_info.fst_size, fst_info.fst_offset );
	if( ret < 0 )
	{
		BootStatusError(-2, -9);
		mdelay(5000);
		//gprintf("WDVD_Read( fst_buffer ): %d\n", ret );
		goto error;
	}

	//set the pointers
	fst = (FST_ENTRY *)fst_buffer;
	if( fst->filelen == 0 || fst->filelen > (fst_info.fst_size / sizeof(FST_ENTRY)) )
		goto error;

	u32 name_table_offset = fst->filelen * sizeof(FST_ENTRY);
	if( name_table_offset >= fst_info.fst_size )
		goto error;

	fst_buffer_size = fst_info.fst_size;
	fst_entry_count = fst->filelen;
	name_table = (char *)( fst_buffer + name_table_offset );
	name_table_size = fst_buffer_size - name_table_offset;
	if( !validate_fst_entries() )
		goto error;

	//clear the openFile and openDir entries
	memset( &openDir, 0, sizeof( OpenFstItem ) );
	memset( &openFile, 0, sizeof( OpenFstItem ) );

	//gprintf("read_disc ok\n");
	return true;

error:
	//gprintf("read_disc fail\n");
	if( fst_buffer )
	{
		free( fst_buffer );
		fst_buffer = NULL;
	}

	fst = NULL;
	name_table = NULL;
	fst_buffer_size = 0;
	fst_entry_count = 0;
	name_table_size = 0;

	return false;
}

static bool alreadyMounted = false;

bool WDVD_FST_IsMounted()
{
	return alreadyMounted;
}

bool WDVD_FST_Mount()
{
	if( alreadyMounted )
		WDVD_FST_Unmount();

	bool success = read_disc();
	if( !success )
		WDVD_FST_Unmount();

	alreadyMounted = success;

	return success;
}

bool WDVD_FST_Unmount()
{
	//gprintf("FST_Unmount()\n");

	alreadyMounted = false;
	memset( &openFile, 0, sizeof( OpenFstItem ) );
	if( fst_buffer )
	{
		free( fst_buffer );
		fst_buffer = NULL;
	}
	name_table = NULL;
	fst = NULL;
	fst_buffer_size = 0;
	fst_entry_count = 0;
	name_table_size = 0;

	return true;
}

#define IOCTL_DI_READID		0x70
#define IOCTL_DI_READ		0x71
#define IOCTL_DI_WAITCVRCLOSE	0x79
#define IOCTL_DI_GETCOVER	0x88
#define IOCTL_DI_RESET		0x8A
#define IOCTL_DI_UNENCREAD	0x8D

static u32 inbuf[8]  ALIGNED(32);
static u32 outbuf[8] ALIGNED(32);

static const char di_fs[] ALIGNED(32) = "/dev/di";
static s32 di_fd = -1;

s32 WDVD_Init(void) {
	if (di_fd < 0) {
		di_fd = IOS_Open(di_fs, 0);
		if (di_fd < 0)
			return di_fd;
	}
	//gprintf("WDVD_Init() ok di_fd: %d\n", di_fd );

	return 0;
}

s32 WDVD_Close(void) {
	UifDiscPartition_CloseIfOpen();

	if (di_fd >= 0) {
	IOS_Close(di_fd);
	di_fd = -1;
	}

	return 0;
}

s32 WDVD_GetHandle()
{
	return di_fd;
}

s32 WDVD_Reset(void) {
	s32 ret;

	memset(inbuf, 0, sizeof(inbuf));

	inbuf[0] = IOCTL_DI_RESET << 24;
	inbuf[1] = 1;

	ret = IOS_Ioctl(di_fd, IOCTL_DI_RESET, inbuf, sizeof(inbuf), outbuf, sizeof(outbuf));
	if (ret < 0)
	return ret;

	return (ret == 1) ? 0 : -ret;
}

s32 WDVD_ReadDiskId(void *id) {
	s32 ret;

	memset(inbuf, 0, sizeof(inbuf));

	inbuf[0] = IOCTL_DI_READID << 24;

	ret = IOS_Ioctl(di_fd, IOCTL_DI_READID, inbuf, sizeof(inbuf), outbuf, sizeof(outbuf));
	if (ret < 0)
	return ret;

	if (ret == 1) {
		memcpy(id, outbuf, 32);
		return 0;
	}

	return -ret;
}

s32 WDVD_UnencryptedRead(void *buf, u32 len, u64 offset) {
	s32 ret;

	memset(inbuf, 0, sizeof(inbuf));

	inbuf[0] = IOCTL_DI_UNENCREAD << 24;
	inbuf[1] = len;
	inbuf[2] = (u32)(offset >> 2);

	ret = IOS_Ioctl(di_fd, IOCTL_DI_UNENCREAD, inbuf, sizeof(inbuf), buf, len);
	if (ret < 0)
	{
		BootStatusError(-2, -17);
		mdelay(5000);
		return ret;
	}
	return (ret == 1) ? 0 : -ret;
}

s32 WDVD_Read(void *buf, u32 len, u64 offset) {
	s32 ret;
	void *io_buf = buf;
	u32 io_len = len;
	bool use_bounce = len > 0 && (!is_aligned_32(buf) || (len & 31u) != 0);

	memset(inbuf, 0, sizeof(inbuf));

	inbuf[0] = IOCTL_DI_READ << 24;
	if( use_bounce )
		io_len = align32_size( len );
	inbuf[1] = io_len;
	inbuf[2] = (u32)(offset >> 2);

	if( use_bounce )
	{
		io_buf = malloca( io_len, 32 );
		if( !io_buf )
			return -1;
	}

	ret = IOS_Ioctl(di_fd, IOCTL_DI_READ, inbuf, sizeof(inbuf), io_buf, io_len);
	if( ret == 1 && use_bounce )
		memcpy(buf, io_buf, len);

	if( use_bounce )
		free(io_buf);

	if (ret < 0)
	return ret;

	return (ret == 1) ? 0 : -ret;
}

s32 WDVD_WaitForDisc(void) {
	s32 ret;

	memset(inbuf, 0, sizeof(inbuf));

	inbuf[0] = IOCTL_DI_WAITCVRCLOSE << 24;

	ret = IOS_Ioctl(di_fd, IOCTL_DI_WAITCVRCLOSE, inbuf, sizeof(inbuf), outbuf, sizeof(outbuf));
	if (ret < 0)
	return ret;

	return (ret == 1) ? 0 : -ret;
}

s32 WDVD_GetCoverStatus(u32 *status) {
	s32 ret;

	memset(inbuf, 0, sizeof(inbuf));

	inbuf[0] = IOCTL_DI_GETCOVER << 24;

	ret = IOS_Ioctl(di_fd, IOCTL_DI_GETCOVER, inbuf, sizeof(inbuf), outbuf, sizeof(outbuf));
	if (ret < 0)
	return ret;

	if (ret == 1) {
	memcpy(status, outbuf, sizeof(u32));

	return 0;
	}

	return -ret;
}
