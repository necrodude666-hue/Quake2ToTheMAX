#ifndef BDGN_RENDER_SCALE_H
#define BDGN_RENDER_SCALE_H

enum { POST_FXAA=1, POST_BLOOM=2, POST_SSAO=4, POST_ADAPTIVE=8, POST_TONEMAP=16, POST_GRADE=32, POST_BRIGHTNESS=64 };

#define RENDER_SCALE_STEPS 20
#define RENDER_SCALE_DEFAULT_STEP 10
#define RENDER_SCALE_MAX 150
#define RENDER_SCALE_STEP 5
#define RENDER_SCALE_MIN 50

static int RenderScaleStep(float value)
{
 if (!(value >= RENDER_SCALE_MIN && value <= RENDER_SCALE_MAX))
  return RENDER_SCALE_DEFAULT_STEP;
 return (int)((value - RENDER_SCALE_MIN) / RENDER_SCALE_STEP + 0.5f);
}

static int RenderScalePercent(int step)
{
 return RENDER_SCALE_MIN + step * RENDER_SCALE_STEP;
}

static int RenderScaleWidth(int width,int percent)
{
 return (width * percent / 100) & ~7;
}

static int RenderScaleHeight(int height,int percent)
{
 return (height * percent / 100) & ~1;
}
#endif
