/* BDGN 3.60c - bdgn_unzip: unpacks a texture pack zip of the #Quake2ToTheMAX installer.

   bdgn_unzip <file.zip> <folder>

   Reads the zips bdgn_packtool writes: "stored" files (no compression), zip64 (over 4 GB). Every file is checked
   (CRC-32) and written under <folder>; names with "..", a drive or a leading slash are refused. A file that is
   already there with the same size and CRC is left alone, so an interrupted install can be run again. Win32 API only
   (Windows 95 to 11), C89. Exit code 0 = all done, 1 = error (the message says which file).
   No C runtime on Windows (KERNEL32 only, built with -nostdlib): runs on the first Windows 95, which has no
   MSVCRT.DLL. */
#include "bdgn_io.h"
#ifndef _WIN32
#include <stdio.h>
#include <stdlib.h>
#endif

static unsigned char buf[1<<20];
static unsigned char tailbuf[65557];

static unsigned slen (const char *s) { unsigned n=0; while (s[n]) n++; return n; }
static const char *schr (const char *s, char c) { for (;*s;s++) if (*s==c) return s; return 0; }
static void out (int err, const char *s)
{
#ifdef _WIN32
	DWORD w;
	WriteFile(GetStdHandle(err?STD_ERROR_HANDLE:STD_OUTPUT_HANDLE),s,slen(s),&w,NULL);
#else
	fputs(s,err?stderr:stdout);
#endif
}
static void quit (int code)
{
#ifdef _WIN32
	ExitProcess(code);
#else
	exit(code);
#endif
}
static void fail (const char *msg, const char *arg)
{
	out(1,"bdgn_unzip: "); out(1,msg);
	if (arg) { out(1," "); out(1,arg); }
	out(1,"\r\n");
	quit(1);
}
static void say (const char *a, const char *b) { out(0,a); out(0,b); out(0,"\r\n"); }

static int safe_name (const char *n)
{
	const char *p;
	if (!n[0] || n[0]=='/' || n[0]=='\\' || schr(n,':')) return 0;
	for (p=n;*p;p++) if ((p==n || p[-1]=='/' || p[-1]=='\\') && p[0]=='.' && p[1]=='.' && (!p[2] || p[2]=='/' || p[2]=='\\')) return 0;
	return 1;
}

static void make_dirs (char *path)
{
	char *p;
	for (p=path;*p;p++) if (*p=='\\' || *p=='/') {
		char c=*p;
		if (p==path || p[-1]==':') continue;
		*p=0; CreateDirectoryA(path,NULL); *p=c;
	}
}

/* 1 when the file is already on the disk with this size and CRC */
static int already_there (const char *path, u64 size, u32 crc)
{
	HANDLE h=io_open_read(path);
	u32 c=0;
	u64 left;
	if (h==INVALID_HANDLE_VALUE) return 0;
	if (io_size(h)!=size) { CloseHandle(h); return 0; }
	for (left=size;left;) {
		u32 n=left>sizeof(buf)?(u32)sizeof(buf):(u32)left;
		if (!io_read(h,buf,n)) { CloseHandle(h); return 0; }
		c=crc_update(c,buf,n); left-=n;
	}
	CloseHandle(h);
	return c==crc;
}

static int run (int argc, char **argv)
{
	HANDLE z;
	u64 zsize, cd=0, count=0, i, tail, eocd=0, pos;
	unsigned char *t;
	unsigned char h[64];
	if (argc!=3) { say("bdgn_unzip <file.zip> <folder>",""); return 1; }
	crc_init();
	z=io_open_read(argv[1]);
	if (z==INVALID_HANDLE_VALUE) fail("cannot open",argv[1]);
	zsize=io_size(z);
	tail=zsize<65557?zsize:65557;
	t=tailbuf;
	if (!io_seek(z,zsize-tail) || !io_read(z,t,(u32)tail)) fail("cannot read",argv[1]);
	for (i=tail-22+1;i-->0;) if (get32(t+i)==0x06054b50) { eocd=zsize-tail+i; break; }
	if (!eocd && get32(t)!=0x06054b50) fail("not a zip file:",argv[1]);
	{
		const unsigned char *e=t+(eocd-(zsize-tail));
		count=get16(e+10); cd=get32(e+16);
		if (eocd>=20 && (count==0xFFFF || cd==0xFFFFFFFF)) {	/* zip64 */
			if (!io_seek(z,eocd-20) || !io_read(z,h,20) || get32(h)!=0x07064b50) fail("broken zip64 record in",argv[1]);
			pos=get64(h+8);
			if (!io_seek(z,pos) || !io_read(z,h,56) || get32(h)!=0x06064b50) fail("broken zip64 directory in",argv[1]);
			count=get64(h+32); cd=get64(h+48);
		}
	}
	pos=cd;
	for (i=0;i<count;i++) {
		u32 namelen, extralen, commentlen, method, crc, c=0, lnamelen, lextralen;
		u64 csize, usize, local, left;
		char name[300], dst[MAX_PATH*2];
		unsigned char ex[256];
		HANDLE o;
		char *p; (void)p;
		if (!io_seek(z,pos) || !io_read(z,h,46) || get32(h)!=0x02014b50) fail("broken directory in",argv[1]);
		method=get16(h+10); crc=get32(h+16); csize=get32(h+20); usize=get32(h+24);
		namelen=get16(h+28); extralen=get16(h+30); commentlen=get16(h+32); local=get32(h+42);
		if (namelen>=sizeof(name) || extralen>sizeof(ex)) fail("name too long in",argv[1]);
		if (!io_read(z,name,namelen) || !io_read(z,ex,extralen)) fail("cannot read",argv[1]);
		name[namelen]=0;
		{	/* zip64 extra: the fields that are 0xFFFFFFFF, in this order */
			u32 k=0;
			while (k+4<=extralen) {
				u32 id=get16(ex+k), sz=get16(ex+k+2), q=k+4;
				if (id==1) {
					if (usize==0xFFFFFFFF && q+8<=k+4+sz) { usize=get64(ex+q); q+=8; }
					if (csize==0xFFFFFFFF && q+8<=k+4+sz) { csize=get64(ex+q); q+=8; }
					if (local==0xFFFFFFFF && q+8<=k+4+sz) { local=get64(ex+q); q+=8; }
				}
				k+=4+sz;
			}
		}
		pos+=46+namelen+extralen+commentlen;
		if (namelen && (name[namelen-1]=='/' || name[namelen-1]=='\\')) continue;	/* a folder */
		if (!safe_name(name)) fail("unsafe name in the zip:",name);
		if (method!=0 || csize!=usize) fail("compressed file (only stored zips are supported):",name);
		if (slen(argv[2])+namelen+2>=sizeof(dst)) fail("path too long:",name);
		{ unsigned a=slen(argv[2]), b; for (b=0;b<a;b++) dst[b]=argv[2][b]; dst[a]='/'; for (b=0;b<=namelen;b++) dst[a+1+b]=name[b]; }
#ifdef _WIN32
		for (p=dst;*p;p++) if (*p=='/') *p='\\';
#endif
		if (!io_seek(z,local) || !io_read(z,h,30) || get32(h)!=0x04034b50) fail("broken file header:",name);
		lnamelen=get16(h+26); lextralen=get16(h+28);
		if (local+30+lnamelen+lextralen+usize>zsize) fail("zip is cut short at",name);
		if (already_there(dst,usize,crc)) { say("ok (already there) ",name); continue; }
		make_dirs(dst);
		o=io_open_write(dst);
		if (o==INVALID_HANDLE_VALUE) fail("cannot create",dst);
		if (!io_seek(z,local+30+lnamelen+lextralen)) fail("seek error at",name);
		for (left=usize;left;) {
			u32 n=left>sizeof(buf)?(u32)sizeof(buf):(u32)left;
			if (!io_read(z,buf,n)) fail("read error at",name);
			if (!io_write(o,buf,n)) fail("write error (disk full?):",dst);
			c=crc_update(c,buf,n); left-=n;
		}
		CloseHandle(o);
		if (c!=crc) { DeleteFileA(dst); fail("CRC error (the zip is damaged):",name); }
		say("ok ",name);
	}
	CloseHandle(z);
	return 0;
}

#ifdef _WIN32
/* the entry point without a C runtime: split GetCommandLineA() as the C runtime would (quotes, no escapes needed for
   paths) */
void __cdecl bdgn_start (void)
{
	static char line[2048];
	static char *argv[8];
	int argc=0;
	const char *c=GetCommandLineA();
	char *d=line, *e=line+sizeof(line)-1;
	while (*c && argc<8) {
		while (*c==' ' || *c=='\t') c++;
		if (!*c) break;
		argv[argc++]=d;
		{
			int q=0;
			while (*c && (q || (*c!=' ' && *c!='\t'))) {
				if (*c=='"') q=!q; else if (d<e) *d++=*c;
				c++;
			}
		}
		if (d<e) *d++=0;
	}
	*d=0;
	quit(run(argc,argv));
}
#else
int main (int argc, char **argv) { return run(argc,argv); }
#endif
