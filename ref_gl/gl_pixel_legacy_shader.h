/* Existing 3.60i shader, retained as the driver fallback. */
static const char legacy_pixel_shader[] =
  "#version 110\n"
  "uniform vec4 lightPos[4],lightCol[4]; uniform sampler2D baseTexture; uniform vec2 texel,sourceTexel; uniform float bump,heightLod;\n"
  "varying vec3 P,N,S,T;\n"
  "float lum(vec3 c){return dot(c,vec3(0.299,0.587,0.114));}\n"
  /* Colour noise in an HD replacement is not extra geometry. Filter height
     at the original material density, while colour keeps its normal LOD. */
  "float heightAt(vec2 uv,float bias){return lum(texture2D(baseTexture,uv,bias).rgb);}\n"
  "void main(){vec2 uv=gl_TexCoord[0].xy; vec4 base=texture2D(baseTexture,uv);"
  "vec3 g=normalize(N); vec3 n=g;"
  "if(bump>0.){"
  "vec2 dx=dFdx(uv)/sourceTexel,dy=dFdy(uv)/sourceTexel;"
  "float lod=.5*log2(max(1.,min(dot(dx,dx),dot(dy,dy))));"
  "float bias=max(0.,heightLod-lod);"
  "float hs=.5*(heightAt(uv+vec2(texel.x,0.),bias)-heightAt(uv-vec2(texel.x,0.),bias));"
  "float ht=.5*(heightAt(uv+vec2(0.,texel.y),bias)-heightAt(uv-vec2(0.,texel.y),bias));"
  "n=normalize(g-bump*(hs*normalize(S)+ht*normalize(T)));}"
  "vec3 sum=vec3(0.);"
  "for(int i=0;i<4;i++){vec3 d=lightPos[i].xyz-P; float len=length(d)+0.001; vec3 l=d/len;"
  "float a=clamp(1.-len*lightPos[i].w,0.,1.); a=a*a*(3.-2.*a);"
  "float face=step(0.,dot(g,l));"
  "sum+=lightCol[i].rgb*a*face*(0.3+0.7*max(dot(n,l),0.));}"
  "gl_FragColor=vec4(base.rgb*sum,0.);}";
