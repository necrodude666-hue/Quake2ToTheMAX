/* Filter in RGB and quantize once, after mixing both mip levels. */
#ifndef BDGN_SW_TEXFILTER_H
#define BDGN_SW_TEXFILTER_H

extern byte sw_filter_palette[32768];
extern int sw_texture_filter_mode;

static __inline unsigned SW_FilterMix(unsigned a,unsigned b,unsigned weight)
{
 unsigned rb=((a&0xff00ff)*(256-weight)+(b&0xff00ff)*weight)>>8;
 unsigned g=((a&0xff00)*(256-weight)+(b&0xff00)*weight)>>8;
 return (rb&0xff00ff)|(g&0xff00);
}

typedef struct { unsigned rgb; byte index; int exact; } sw_filter_color_t;

static __inline sw_filter_color_t SW_FilterSample(const byte *base,int width,int height,
 int s,int t,const byte *shade,const byte *remap)
{
 int x,y,u,v,right,bottom,a,b,c,d;
 sw_filter_color_t result;
 s=s>0x8000?s-0x8000:0;
 t=t>0x8000?t-0x8000:0;
 x=s>>16; y=t>>16;
 u=(s>>8)&255; v=(t>>8)&255;
 if(x>=width) { x=width-1; u=0; }
 if(y>=height) { y=height-1; v=0; }
 base+=y*width+x;
 right=x+1<width;
 bottom=y+1<height?width:0;
 a=base[0]; b=base[right]; c=base[bottom]; d=base[bottom+right];
 if(shade) { a=shade[a]; b=shade[b]; c=shade[c]; d=shade[d]; }
 if(remap) { a=remap[a]; b=remap[b]; c=remap[c]; d=remap[d]; }
 result.index=(byte)a;
 result.exact=(!u&&!v)||(a==b&&a==c&&a==d);
 result.rgb=d_8to24table[a]&0xffffff;
 if(!result.exact)
  result.rgb=SW_FilterMix(SW_FilterMix(result.rgb,d_8to24table[b],u),
   SW_FilterMix(d_8to24table[c],d_8to24table[d],u),v);
 return result;
}

static __inline byte SW_FilterPalette(unsigned rgb)
{
 return sw_filter_palette[((rgb>>3)&31)|((rgb>>6)&992)|((rgb>>9)&31744)];
}

static __inline byte SW_BilinearTexel(const byte *base,int width,int height,int s,int t,
 const byte *shade,const byte *remap)
{
 sw_filter_color_t c=SW_FilterSample(base,width,height,s,t,shade,remap);
 return c.exact?c.index:SW_FilterPalette(c.rgb);
}
#endif
