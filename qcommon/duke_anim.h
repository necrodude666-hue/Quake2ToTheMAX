/* Read-only, pre-generated indexed sprite tiles. Shared by both renderers.
 * BDGN 3.51q: two tile sizes. 64x64 (sprites/anim/, bdgn_anim.pak, as in 3.51p) and 32x32 (sprites/anim32/,
 * bdgn_anim32.pak). The size is read from each bank header; buffers are sized for the largest (64). */
#ifndef BDGN_DUKE_ANIM_H
#define BDGN_DUKE_ANIM_H
#define DUKE_TILE 64			/* largest tile: buffer size */
#define DUKE_SLOTS 512
#define DUKE_BANK_LIMIT (24*1024*1024)
typedef struct { short width,height,ox,oy; } duke_record_t;
#define DUKE_FRAME_BYTES_T(t) (5*8+5*(t)*(t))	/* one direction strip: 5 records + 5 tiles */
#define DUKE_FRAME_BYTES DUKE_FRAME_BYTES_T(DUKE_TILE)
/* tile size of a valid bank (32 or 64), 0 = invalid */
static int Duke_ValidBank(const byte *data,int size)
{
 const int *h;int i,n,t,fb;const unsigned *r;
 if(!data || size<32)return 0;h=(const int *)data;
 t=h[4];if(t!=32 && t!=64)return 0;fb=DUKE_FRAME_BYTES_T(t);
 if(h[0]!=0x31415344 || h[1]!=4 || h[2]<1 || h[2]>4096 || h[3]!=5)return 0;
 n=h[2]*8;if(h[5]!=h[2]*40 || h[6]!=32+n*12 || h[7]!=size || h[6]!=size)return 0;
 r=(const unsigned *)(data+32);
 for(i=0;i<n;i++)if(r[i*3]>(unsigned)(64*fb*2) || r[i*3+1]>(unsigned)(fb*2) || r[i*3+2]!=(unsigned)fb)return 0;
 return t;
}
/* Bounds-checked LZ4 block decoder. Compressed assets, never model geometry. */
static int Duke_Expand(const byte *src,int size,byte *dst,int capacity)
{
 int p=0,o=0,token,n,x,off,i;
 while(p<size) {
  token=src[p++];n=token>>4;
  if(n==15)do{if(p>=size)return 0;x=src[p++];if(n>capacity-x)return 0;n+=x;}while(x==255);
  if(n>size-p || n>capacity-o)return 0;memcpy(dst+o,src+p,n);p+=n;o+=n;if(p==size)break;
  if(size-p<2)return 0;off=src[p]|(src[p+1]<<8);p+=2;n=(token&15)+4;
  if((token&15)==15)do{if(p>=size)return 0;x=src[p++];if(n>capacity-x)return 0;n+=x;}while(x==255);
  if(off<1 || off>o || n>capacity-o)return 0;for(i=0;i<n;i++)dst[o+i]=dst[o-off+i];o+=n;
 }
 return o==capacity;
}
static int Duke_PoseIndex(int frame,int oldframe,float backlerp,int frames,int direction)
{
 int phase=0;if(frame<0 || frame>=frames)frame=0;
 if(oldframe==frame-1 && backlerp>=0 && backlerp<=1)phase=(int)(backlerp*5+0.5f);
 if(phase>4)phase=4;if(direction<0 || direction>7)direction=0;
 return (frame*5+phase)*8+direction;
}
#endif
