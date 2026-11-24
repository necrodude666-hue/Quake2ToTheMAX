/* Clipped nearest-neighbour paletted blit. C89; also used by the tests. */
#ifndef BDGN_SW_BLIT_H
#define BDGN_SW_BLIT_H
static void SW_Blit8(unsigned char *dst, int dw, int dh, int stride,
 const unsigned char *src, int sw, int sh, int ss,
 int x, int y, int w, int h, int transparent)
{
 int u, v, left, top, right, bottom, sx, sy, step, start, remainder, error;
 unsigned char *d;
 const unsigned char *s;
 if (!dst || !src || sw<=0 || sh<=0 || w<=0 || h<=0) return;
 left=x<0?0:x; top=y<0?0:y;
 right=x+w>dw?dw:x+w; bottom=y+h>dh?dh:y+h;
 if(left>=right || top>=bottom) return;
 step=sw/w; remainder=sw%w; start=(left-x)*sw;
 for(v=top;v<bottom;v++) {
  sy=(v-y)*sh/h; s=src+sy*ss; d=dst+v*stride;
  if(sw==1 && sh==1 && !transparent) { memset(d+left,*src,right-left); continue; }
  /* Preserve the original copy path at 1:1, including odd picture widths. */
  if(w==sw && !transparent) { memcpy(d+left,s+left-x,right-left); continue; }
  /* BDGN 3.56c: 1:1 with transparency (HUD characters, icons): no stepping, same pixels */
  if(w==sw && h==sh) { for(u=left;u<right;u++) if(s[u-x]!=255) d[u]=s[u-x]; continue; }
  sx=start/w; error=start%w;
  for(u=left;u<right;u++) {
   if(!transparent || s[sx]!=255) d[u]=s[sx];
   sx+=step; error+=remainder;
   if(error>=w) { error-=w; sx++; }
  }
 }
}
#endif
