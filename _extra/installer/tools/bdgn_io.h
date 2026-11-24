/* BDGN 3.60c: tiny 64-bit file I/O on the Win32 API only (CreateFileA / SetFilePointer / ReadFile / WriteFile), so the
   tools run on Windows 95 to 11 and handle files over 2 GB (zip64). C89. */
#ifndef BDGN_IO_H
#define BDGN_IO_H
#ifdef _WIN32
#include <windows.h>
typedef unsigned __int64 u64;
#else	/* POSIX build, only for testing the tools on Linux */
#define _FILE_OFFSET_BITS 64
#include <stdio.h>
#include <strings.h>
#include <sys/stat.h>
typedef unsigned long long u64;
typedef FILE *HANDLE;
#define INVALID_HANDLE_VALUE NULL
#define CloseHandle fclose
#define _stricmp strcasecmp
#define _strtoui64 strtoull
#define CreateDirectoryA(p,s) (mkdir(p,0755)==0)
#define DeleteFileA remove
#define MAX_PATH 260
#endif
typedef unsigned int u32;
#ifdef _WIN32

static HANDLE io_open_read (const char *path)
{
	return CreateFileA(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_SEQUENTIAL_SCAN,NULL);
}
static HANDLE io_open_write (const char *path)
{
	return CreateFileA(path,GENERIC_READ|GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
}
static int io_seek (HANDLE h, u64 pos)
{
	LONG hi=(LONG)(pos>>32);
	DWORD lo=SetFilePointer(h,(LONG)(DWORD)pos,&hi,FILE_BEGIN);
	if (lo==0xFFFFFFFF && GetLastError()!=NO_ERROR) return 0;
	return 1;
}
static u64 io_size (HANDLE h)
{
	DWORD hi=0, lo=GetFileSize(h,&hi);
	return ((u64)hi<<32)|lo;
}
static u64 io_tell (HANDLE h)
{
	LONG hi=0;
	DWORD lo=SetFilePointer(h,0,&hi,FILE_CURRENT);
	return ((u64)(DWORD)hi<<32)|lo;
}
static int io_read (HANDLE h, void *buf, u32 n)
{
	DWORD got=0;
	return ReadFile(h,buf,n,&got,NULL) && got==n;
}
static int io_write (HANDLE h, const void *buf, u32 n)
{
	DWORD put=0;
	return WriteFile(h,buf,n,&put,NULL) && put==n;
}

#else
static HANDLE io_open_read (const char *path) { return fopen(path,"rb"); }
static HANDLE io_open_write (const char *path) { return fopen(path,"w+b"); }
static int io_seek (HANDLE h, u64 pos) { return fseeko(h,(off_t)pos,SEEK_SET)==0; }
static u64 io_size (HANDLE h) { off_t c=ftello(h),e; fseeko(h,0,SEEK_END); e=ftello(h); fseeko(h,c,SEEK_SET); return (u64)e; }
static u64 io_tell (HANDLE h) { return (u64)ftello(h); }
static int io_read (HANDLE h, void *buf, u32 n) { return fread(buf,1,n,h)==n; }
static int io_write (HANDLE h, const void *buf, u32 n) { return fwrite(buf,1,n,h)==n; }
#endif

static u32 crc_table[256];
static void crc_init (void)
{
	u32 i,j,c;
	for (i=0;i<256;i++) { c=i; for (j=0;j<8;j++) c=(c&1)?0xEDB88320u^(c>>1):c>>1; crc_table[i]=c; }
}
static u32 crc_update (u32 crc, const unsigned char *p, u32 n)
{
	crc=~crc;
	while (n--) crc=crc_table[(crc^*p++)&0xFF]^(crc>>8);
	return ~crc;
}

static void put16 (unsigned char *p, u32 v) { p[0]=(unsigned char)v; p[1]=(unsigned char)(v>>8); }
static void put32 (unsigned char *p, u32 v) { put16(p,v&0xFFFF); put16(p+2,v>>16); }
static void put64 (unsigned char *p, u64 v) { put32(p,(u32)v); put32(p+4,(u32)(v>>32)); }
static u32 get16 (const unsigned char *p) { return p[0]|(p[1]<<8); }
static u32 get32 (const unsigned char *p) { return get16(p)|(get16(p+2)<<16); }
static u64 get64 (const unsigned char *p) { return get32(p)|((u64)get32(p+4)<<32); }

#endif
