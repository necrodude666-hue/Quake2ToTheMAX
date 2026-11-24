/* BDGN 3.60c - bdgn_packtool: builds the texture pack zips of the #Quake2ToTheMAX installer.

   bdgn_packtool <out.zip> {-merge <maxbytes> <member> <count> <pak> ...} {-file <member> <path>}

   -merge  merges <count> pak files into one pak inside the zip (several when the result would pass <maxbytes>; a pak
           has signed 32-bit offsets, so stay under 2 GB). The paks are taken in name order, as the game loads them
           (qcommon/files.c FS_ScanQualityPacks: the last name wins), so a file in pak8-hdsd-warp.pak replaces the same
           file in pak8-hdsd-e1.pak. <member> is the path in the zip without ".pak": one pak gets "<member>.pak",
           several get "<member>1.pak", "<member>2.pak" ...
   -file   stores a file as it is.

   When <out.zip> contains '#', every merged pak goes into a zip of its own, '#' replaced by the part number
   (Q2TTM_Textures_AI_4K_part#.zip -> _part1.zip, _part2.zip ...): each file then stays under 2 GB, the limit of a
   GitHub release file. -file is not allowed then.

   The zip is "stored" (no compression: DDS and PCX/TGA hardly shrink, and storing is fast) and zip64, so it may pass
   4 GB. Windows Explorer, 7-Zip and bdgn_unzip.exe (the installer) read it. The merged paks are written straight into
   the zip: no extra copy on the disk. Win32 API only; C89 (VS2005 / MinGW). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bdgn_io.h"

#define MAX_SRC 64
#define MAX_ENTRIES 65536
#define MAX_MEMBERS 256
#define DOS_TIME 0x0000
#define DOS_DATE ((46<<9)|(11<<5)|22)	/* 2026-11-22 */

typedef struct { char name[56]; u32 pos, len; int src; } entry_t;
typedef struct { char name[260]; u64 offset, size; u32 crc; } member_t;

static HANDLE zip=INVALID_HANDLE_VALUE;
static const char *zip_pattern;	/* contains '#': one zip per merged pak */
static void finish (void);
static member_t members[MAX_MEMBERS];
static int member_count;
static unsigned char copybuf[1<<20];

static void fail (const char *msg, const char *arg)
{
	fprintf(stderr,"bdgn_packtool: %s%s%s\n",msg,arg?" ":"",arg?arg:"");
	exit(1);
}

static const char *base_name (const char *p)
{
	const char *a=strrchr(p,'/'), *b=strrchr(p,'\\');
	if (b>a) a=b;
	return a?a+1:p;
}

static int name_cmp (const void *a, const void *b)
{
	return _stricmp(base_name(*(const char *const *)a),base_name(*(const char *const *)b));
}

/* local header with a zip64 extra (sizes known now, CRC patched afterwards) */
static void member_begin (const char *name, u64 size)
{
	unsigned char h[30+260+20];
	u32 n=(u32)strlen(name);
	member_t *m;
	if (member_count>=MAX_MEMBERS) fail("too many files in the zip",NULL);
	if (n>=260) fail("name too long:",name);
	m=&members[member_count++];
	strcpy(m->name,name); m->offset=io_tell(zip); m->size=size; m->crc=0;
	memset(h,0,sizeof(h));
	put32(h,0x04034b50); put16(h+4,45); put16(h+6,0); put16(h+8,0);
	put16(h+10,DOS_TIME); put16(h+12,DOS_DATE); put32(h+14,0);
	put32(h+18,0xFFFFFFFF); put32(h+22,0xFFFFFFFF); put16(h+26,n); put16(h+28,20);
	memcpy(h+30,name,n);
	put16(h+30+n,1); put16(h+32+n,16); put64(h+34+n,size); put64(h+42+n,size);
	if (!io_write(zip,h,30+n+20)) fail("write error",NULL);
}
static void member_data (const void *p, u32 n)
{
	member_t *m=&members[member_count-1];
	m->crc=crc_update(m->crc,(const unsigned char *)p,n);
	if (!io_write(zip,p,n)) fail("write error (disk full?)",NULL);
}
static void member_end (void)
{
	member_t *m=&members[member_count-1];
	u64 here=io_tell(zip);
	unsigned char c[4];
	if (here-m->offset-30-strlen(m->name)-20!=m->size) fail("size mismatch in",m->name);
	put32(c,m->crc);
	if (!io_seek(zip,m->offset+14) || !io_write(zip,c,4) || !io_seek(zip,here)) fail("seek error",NULL);
	printf("  %s  %lu MB  crc %08x\n",m->name,(unsigned long)(m->size>>20),m->crc);
}

static void copy_range (HANDLE h, u64 pos, u64 len, const char *what)
{
	if (!io_seek(h,pos)) fail("seek error in",what);
	while (len) {
		u32 n=len>sizeof(copybuf)?(u32)sizeof(copybuf):(u32)len;
		if (!io_read(h,copybuf,n)) fail("read error in",what);
		member_data(copybuf,n);
		len-=n;
	}
}

static void add_file (const char *member, const char *path)
{
	HANDLE h=io_open_read(path);
	u64 size;
	if (h==INVALID_HANDLE_VALUE) fail("cannot open",path);
	size=io_size(h);
	member_begin(member,size);
	copy_range(h,0,size,path);
	member_end();
	CloseHandle(h);
}

static entry_t *entries;
static int entry_count;
static HANDLE src_h[MAX_SRC];
static const char *src_name[MAX_SRC];

/* the entries [first, return) that fit in one pak of at most maxbytes (at least one) */
static int part_end (int first, u64 maxbytes)
{
	u64 data=0;
	int last=first;
	while (last<entry_count && (last==first || 12+data+entries[last].len+64*(u64)(last-first+1)<=maxbytes)) { data+=entries[last].len; last++; }
	return last;
}

static void merge (u64 maxbytes, const char *member, int count, char **paks)
{
	int i, j, first, part=0, parts=0;
	unsigned char hdr[12];
	char out[260];
	if (count<1 || count>MAX_SRC) fail("-merge: 1 to 64 paks",NULL);
	if (!entries) entries=(entry_t *)malloc(sizeof(entry_t)*MAX_ENTRIES);
	if (!entries) fail("out of memory",NULL);
	entry_count=0;
	qsort(paks,count,sizeof(char *),name_cmp);
	for (i=0;i<count;i++) {
		u32 dirofs, dirlen, k, n;
		unsigned char *dir;
		src_name[i]=paks[i];
		src_h[i]=io_open_read(paks[i]);
		if (src_h[i]==INVALID_HANDLE_VALUE) fail("cannot open",paks[i]);
		if (!io_read(src_h[i],hdr,12) || memcmp(hdr,"PACK",4)) fail("not a pak file:",paks[i]);
		dirofs=get32(hdr+4); dirlen=get32(hdr+8);
		if (dirlen%64 || (u64)dirofs+dirlen>io_size(src_h[i])) fail("broken pak directory:",paks[i]);
		n=dirlen/64;
		dir=(unsigned char *)malloc(dirlen?dirlen:1);
		if (!dir || !io_seek(src_h[i],dirofs) || !io_read(src_h[i],dir,dirlen)) fail("cannot read the directory of",paks[i]);
		for (k=0;k<n;k++) {
			const unsigned char *d=dir+k*64;
			char name[57];
			memcpy(name,d,56); name[56]=0;
			if (!name[0]) continue;
			if ((u64)get32(d+56)+get32(d+60)>io_size(src_h[i])) fail("file outside the pak in",paks[i]);
			for (j=0;j<entry_count;j++) if (!_stricmp(entries[j].name,name)) break;	/* the later pak wins */
			if (j==entry_count) { if (entry_count>=MAX_ENTRIES) fail("too many files",NULL); entry_count++; }
			memset(entries[j].name,0,56); strncpy(entries[j].name,name,55);
			entries[j].pos=get32(d+56); entries[j].len=get32(d+60); entries[j].src=i;
		}
		free(dir);
		printf("  read %s: %u files\n",base_name(paks[i]),n);
	}
	/* how many parts: the same split as below */
	for (first=0;first<entry_count;parts++) first=part_end(first,maxbytes);
	first=0;
	while (first<entry_count) {
		u64 data=0, total;
		int last=part_end(first,maxbytes);
		unsigned char *dir;
		for (i=first;i<last;i++) data+=entries[i].len;
		part++;
		if (parts==1) sprintf(out,"%s.pak",member); else sprintf(out,"%s%d.pak",member,part);
		if (zip_pattern) {	/* a zip of its own for this pak */
			char zname[300];
			const char *h=strchr(zip_pattern,'#');
			if (zip!=INVALID_HANDLE_VALUE) { finish(); CloseHandle(zip); member_count=0; }
			sprintf(zname,"%.*s%d%s",(int)(h-zip_pattern),zip_pattern,part,h+1);
			zip=io_open_write(zname);
			if (zip==INVALID_HANDLE_VALUE) fail("cannot create",zname);
			printf("%s\n",zname);
		}
		total=12+data+64*(u64)(last-first);
		if (total>0x7FFFFFFF) fail("one file alone is too big for a pak in",out);
		member_begin(out,total);
		memcpy(hdr,"PACK",4); put32(hdr+4,(u32)(12+data)); put32(hdr+8,(u32)(64*(last-first)));
		member_data(hdr,12);
		for (i=first;i<last;i++) copy_range(src_h[entries[i].src],entries[i].pos,entries[i].len,src_name[entries[i].src]);
		dir=(unsigned char *)calloc(last-first,64);
		if (!dir) fail("out of memory",NULL);
		{
			u32 pos=12;
			for (i=first;i<last;i++) {
				unsigned char *d=dir+(i-first)*64;
				memcpy(d,entries[i].name,56); put32(d+56,pos); put32(d+60,entries[i].len); pos+=entries[i].len;
			}
		}
		member_data(dir,64*(last-first));
		free(dir);
		member_end();
		first=last;
	}
	printf("  %d files in %d pak(s)\n",entry_count,parts);
	for (i=0;i<count;i++) CloseHandle(src_h[i]);
}

static void finish (void)
{
	u64 cd=io_tell(zip), cdsize, end;
	int i;
	unsigned char h[46+260+28];
	for (i=0;i<member_count;i++) {
		member_t *m=&members[i];
		u32 n=(u32)strlen(m->name);
		memset(h,0,sizeof(h));
		put32(h,0x02014b50); put16(h+4,45); put16(h+6,45); put16(h+8,0); put16(h+10,0);
		put16(h+12,DOS_TIME); put16(h+14,DOS_DATE); put32(h+16,m->crc);
		put32(h+20,0xFFFFFFFF); put32(h+24,0xFFFFFFFF); put16(h+28,n); put16(h+30,28); put16(h+32,0);
		put16(h+34,0); put16(h+36,0); put32(h+38,0); put32(h+42,0xFFFFFFFF);
		memcpy(h+46,m->name,n);
		put16(h+46+n,1); put16(h+48+n,24); put64(h+50+n,m->size); put64(h+58+n,m->size); put64(h+66+n,m->offset);
		if (!io_write(zip,h,46+n+28)) fail("write error",NULL);
	}
	end=io_tell(zip); cdsize=end-cd;
	memset(h,0,56+20+22);
	put32(h,0x06064b50); put64(h+4,44); put16(h+12,45); put16(h+14,45); put32(h+16,0); put32(h+20,0);
	put64(h+24,member_count); put64(h+32,member_count); put64(h+40,cdsize); put64(h+48,cd);
	put32(h+56,0x07064b50); put32(h+60,0); put64(h+64,end); put32(h+72,1);
	put32(h+76,0x06054b50); put16(h+80,0); put16(h+82,0); put16(h+84,0xFFFF); put16(h+86,0xFFFF);
	put32(h+88,0xFFFFFFFF); put32(h+92,0xFFFFFFFF); put16(h+96,0);
	if (!io_write(zip,h,98)) fail("write error",NULL);
}

int main (int argc, char **argv)
{
	int i;
	if (argc<4) {
		printf("bdgn_packtool <out.zip> {-merge <maxbytes> <member> <count> <pak> ...} {-file <member> <path>}\n");
		return 1;
	}
	crc_init();
	if (strlen(argv[1])>250) fail("name too long:",argv[1]);
	if (strchr(argv[1],'#')) zip_pattern=argv[1];
	else {
		zip=io_open_write(argv[1]);
		if (zip==INVALID_HANDLE_VALUE) fail("cannot create",argv[1]);
		printf("%s\n",argv[1]);
	}
	for (i=2;i<argc;) {
		if (!strcmp(argv[i],"-file") && i+2<argc) {
			if (zip_pattern) fail("-file cannot be used with a '#' zip name",NULL);
			add_file(argv[i+1],argv[i+2]); i+=3;
		}
		else if (!strcmp(argv[i],"-merge") && i+3<argc) {
			u64 maxbytes=_strtoui64(argv[i+1],NULL,10);
			int count=atoi(argv[i+3]);
			if (maxbytes<1000000 || maxbytes>2000000000) fail("-merge: maxbytes 1000000 to 2000000000",NULL);
			if (i+4+count>argc) fail("-merge: fewer paks than <count>",NULL);
			merge(maxbytes,argv[i+2],count,argv+i+4);
			i+=4+count;
		}
		else fail("unknown argument",argv[i]);
	}
	if (zip!=INVALID_HANDLE_VALUE) { finish(); CloseHandle(zip); }
	printf("done\n");
	return 0;
}
