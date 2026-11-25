"""Bake a 2:1 spherical source into Quake II's six sky faces.

No source artwork is synthesized here. The face matrices match gl_warp.c.
The shared face borders sample identical rays, including all eight corners.
Usage: python tools/bake_skies.py manifest.json output_directory
"""
import json, math, shutil, sys
from pathlib import Path
import numpy as np
from PIL import Image

NAMES = ('ferrum','crimson','azure','dusk','dawn','day','night','shipyard','fleet','eclipse')
MATRICES = ((3,-1,2),(-3,1,2),(1,3,2),(-1,-3,2),(-2,-1,3),(2,-1,-3))
ORDER = (0,2,1,3,4,5)
SUFFIX = ('rt','bk','lf','ft','up','dn')
GAINS = (.87,.83,.92,.88,1.08,1.18,.76,.82,.94,.79)

def sample(pixels, rays):
    h,w,_=pixels.shape
    r=rays/np.linalg.norm(rays,axis=-1,keepdims=True)
    u=(np.arctan2(r[...,1],r[...,0])/(2*math.pi)+.5)*w-.5
    v=(.5-np.arcsin(np.clip(r[...,2],-1,1))/math.pi)*h-.5
    x0=np.floor(u).astype(int); y0=np.floor(v).astype(int)
    fx=(u-x0)[...,None]; fy=(v-y0)[...,None]
    a=pixels[np.clip(y0,0,h-1),x0%w]
    b=pixels[np.clip(y0,0,h-1),(x0+1)%w]
    c=pixels[np.clip(y0+1,0,h-1),x0%w]
    d=pixels[np.clip(y0+1,0,h-1),(x0+1)%w]
    return np.rint((a*(1-fx)+b*fx)*(1-fy)+(c*(1-fx)+d*fx)*fy).clip(0,255).astype('uint8')

def bake(manifest, dest, size=1024):
    dest=Path(dest); env=dest/'baseq2/env/q2ttm_skies'; env.mkdir(parents=True,exist_ok=True)
    sources=dest/'sources'; sources.mkdir(exist_ok=True)
    profiles=[]; verification=[]
    for index,name in enumerate(NAMES):
        entry=next(x for x in manifest if x['name']==name)
        src=Path(entry['path']); shutil.copy2(src,sources/(name+'.png'))
        p=np.array(Image.open(src).convert('RGB'),dtype=np.float32)
        h,w,_=p.shape
        if w!=2*h: raise ValueError('Expected spherical 2:1 source: '+name)
        # A periodic overlap only at the longitude join avoids a source-border seam.
        # Pole rows are collapsed because every longitude there represents one ray.
        band=min(24,w//64)
        for i in range(band):
            weight=(1-i/band)*.5
            a=p[:,i].copy(); b=p[:,w-1-i].copy()
            p[:,i]=a*(1-weight)+b*weight; p[:,w-1-i]=b*(1-weight)+a*weight
        for y in range(8):
            weight=1-y/8
            p[y]=p[y]*(1-weight)+p[y].mean(axis=0)*weight
            p[h-1-y]=p[h-1-y]*(1-weight)+p[h-1-y].mean(axis=0)*weight
        coord=np.linspace(-1,1,size,dtype=np.float64)
        s,t=np.meshgrid(coord,-coord)
        b=np.stack((s,t,np.ones_like(s)),axis=-1)
        edges={}; all_faces=[]
        for image,axis in enumerate(ORDER):
            ray=np.stack([(1 if k>0 else -1)*b[...,abs(k)-1] for k in MATRICES[axis]],axis=-1)
            rgb=sample(p,ray); all_faces.append(rgb)
            # Pillow writes bottom-origin TGA; LoadTGA returns rows in top-origin order.
            Image.fromarray(rgb).save(env/(name+SUFFIX[image]+'.tga'))
            for row,col in [(0,j) for j in range(size)]+[(size-1,j) for j in range(size)]+[(j,0) for j in range(size)]+[(j,size-1) for j in range(size)]:
                key=tuple(np.round(ray[row,col],10))
                val=rgb[row,col]
                if key in edges and not np.array_equal(edges[key],val): raise AssertionError('Cube edge mismatch')
                edges[key]=val
        # Select a spatially averaged bright source ABOVE the horizon. Store its
        # actual spherical direction so god rays point to the feature in the art.
        small=np.asarray(Image.fromarray(p.clip(0,255).astype('uint8')).resize((128,64),Image.Resampling.BOX),dtype=float)
        lum=small[...,:3]@np.array([.299,.587,.114]); lum[28:]=0
        yy,xx=np.unravel_index(np.argmax(lum),lum.shape)
        lon=((xx+.5)/128-.5)*2*math.pi; lat=(.5-(yy+.5)/64)*math.pi
        direction=(math.cos(lat)*math.cos(lon),math.cos(lat)*math.sin(lon),math.sin(lat))
        colour=small[yy,xx]; colour=colour/max(1,colour.max())
        mean=small.mean(axis=(0,1)); tint=mean/max(1,mean.mean()); tint=np.clip(1+(tint-1)*.13,.90,1.10)
        profiles.append({'name':name,'gain':GAINS[index],'tint':tint.tolist(),'direction':direction,'sun':colour.tolist()})
        verification.append({'sky':name,'faces':6,'size':size,'shared_edges':'identical','source':src.name})
    (env/'catalog.txt').write_text('Q2TTM_SKIES_1\n'+'\n'.join(NAMES)+'\n',encoding='ascii')
    (dest/'profiles.json').write_text(json.dumps(profiles,indent=2),encoding='utf-8')
    (dest/'verification.json').write_text(json.dumps(verification,indent=2),encoding='utf-8')
    header=['/* Generated from sky artwork by tools/bake_skies.py. */',
      '#ifndef Q2TTM_SKY_PROFILES_H','#define Q2TTM_SKY_PROFILES_H',
      'typedef struct { const char *name; float gain, tint[3], direction[3], sun[3]; } q2ttm_sky_profile_t;',
      'static const q2ttm_sky_profile_t q2ttm_sky_profiles[10] = {']
    vec=lambda v:'{'+','.join('%.7ff'%x for x in v)+'}'
    for p in profiles: header.append(' {"q2ttm_skies/%s",%.7ff,%s,%s,%s},'%(p['name'],p['gain'],vec(p['tint']),vec(p['direction']),vec(p['sun'])))
    header+=['};','#endif','']
    (dest/'sky_profiles.h').write_text('\n'.join(header),encoding='ascii')
    print(json.dumps({'skies':len(profiles),'faces':60,'shared_edge_check':'passed','directory':str(env)}))

if __name__=='__main__': bake(json.loads(Path(sys.argv[1]).read_text(encoding='utf-8')),sys.argv[2])
