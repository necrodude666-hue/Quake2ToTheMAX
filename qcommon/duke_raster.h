/* Offline generation/test only. Never included by the game renderers. */
#ifndef BDGN_DUKE_RASTER_H
#define BDGN_DUKE_RASTER_H
#ifndef DUKE_SIZE
#define DUKE_SIZE 128
#endif
#define DUKE_CACHE 64
#define DUKE_SKIN_CACHE 32
typedef struct {
 const dmdl_t *hdr;
 const byte *skin;
 int width, height, frame, oldframe;
 float backlerp;
} duke_mesh_t;
typedef struct {
 byte pixels[DUKE_SIZE*DUKE_SIZE];
 int width, height, origin_x, origin_y;
} duke_picture_t;
static float duke_projected[2][MAX_VERTS][3];
static float duke_depth[DUKE_SIZE*DUKE_SIZE];
static float Duke_Edge(const float *a,const float *b,float x,float y)
{ return (b[0]-a[0])*(y-a[1])-(b[1]-a[1])*(x-a[0]); }
static int Duke_Raster(const duke_mesh_t *meshes,int count,int direction,float pitch,float roll,int mask,duke_picture_t *out)
{
 int m,i,k,t,x,y,x0,x1,y0,y1,index,tx,ty,visible=0;
 const dmdl_t *h;
 const daliasframe_t *cur,*old;
 const dstvert_t *st;
 const dtriangle_t *tri;
 float p[3],q[3],a[3],b[3],c[3],uv[3][2];
 float minx=1e30f,maxx=-1e30f,minz=1e30f,maxz=-1e30f;
 float angle=direction*(float)(3.14159265358979323846/4.0),cs=(float)cos(angle),sn=(float)sin(angle);
 float pc=(float)cos(pitch*(3.14159265358979323846/180.0)),ps=(float)sin(pitch*(3.14159265358979323846/180.0));
 float rc=(float)cos(roll*(3.14159265358979323846/180.0)),rs=(float)sin(roll*(3.14159265358979323846/180.0));
 float frac,area,wa,wb,wc,z,u,v;
 byte pixel;
 if(count<1 || count>2) return 0;
 for(m=0;m<count;m++) {
  h=meshes[m].hdr;
  if(!h || h->num_xyz<1 || h->num_xyz>MAX_VERTS || h->num_frames<1 || h->skinwidth<=0 || h->skinheight<=0 || !meshes[m].skin || meshes[m].width<=0 || meshes[m].height<=0) return 0;
  i=meshes[m].frame; if(i<0 || i>=h->num_frames)i=0;
  k=meshes[m].oldframe;if(k<0 || k>=h->num_frames)k=0;
  cur=(const daliasframe_t *)((const byte *)h+h->ofs_frames+i*h->framesize);
  old=(const daliasframe_t *)((const byte *)h+h->ofs_frames+k*h->framesize);
  frac=meshes[m].backlerp; if(frac<0)frac=0;if(frac>1)frac=1;
  for(i=0;i<h->num_xyz;i++) {
   for(k=0;k<3;k++) p[k]=(1-frac)*(cur->verts[i].v[k]*cur->scale[k]+cur->translate[k])+frac*(old->verts[i].v[k]*old->scale[k]+old->translate[k]);
   q[0]=p[0];q[1]=p[1]*rc-p[2]*rs;q[2]=p[1]*rs+p[2]*rc;
   p[0]=q[0]*pc-q[2]*ps;p[1]=q[1];p[2]=q[0]*ps+q[2]*pc;
   q[0]=-sn*p[0]+cs*p[1];q[1]=p[2];q[2]=cs*p[0]+sn*p[1];
   for(k=0;k<3;k++)duke_projected[m][i][k]=q[k];
   if(q[0]<minx)minx=q[0];if(q[0]>maxx)maxx=q[0];if(q[1]<minz)minz=q[1];if(q[1]>maxz)maxz=q[1];
  }
 }
 out->width=(int)ceil(maxx-minx)+4;out->height=(int)ceil(maxz-minz)+4;
 out->origin_x=(int)floor(-minx+2.5f);out->origin_y=(int)floor(-minz+2.5f);
 if(out->width<1 || out->height<1) return 0;
 memset(out->pixels,255,sizeof(out->pixels));for(i=0;i<DUKE_SIZE*DUKE_SIZE;i++)duke_depth[i]=-1e30f;
 for(m=0;m<count;m++) {
  h=meshes[m].hdr;st=(const dstvert_t *)((const byte *)h+h->ofs_st);tri=(const dtriangle_t *)((const byte *)h+h->ofs_tris);
  for(t=0;t<h->num_tris;t++) {
   float *verts[3];verts[0]=a;verts[1]=b;verts[2]=c;
   for(i=0;i<3;i++) {
    index=tri[t].index_xyz[i];k=tri[t].index_st[i];
    if(index<0 || index>=h->num_xyz || k<0 || k>=h->num_st) return 0;
    verts[i][0]=(duke_projected[m][index][0]+out->origin_x)*DUKE_SIZE/out->width;
    verts[i][1]=(out->height-out->origin_y-duke_projected[m][index][1])*DUKE_SIZE/out->height;
    verts[i][2]=duke_projected[m][index][2];uv[i][0]=(float)st[k].s/h->skinwidth;uv[i][1]=(float)st[k].t/h->skinheight;
   }
   area=Duke_Edge(a,b,c[0],c[1]);if(area>-0.00001f && area<0.00001f)continue;
   minx=a[0];maxx=a[0];minz=a[1];maxz=a[1];for(i=1;i<3;i++){if(verts[i][0]<minx)minx=verts[i][0];if(verts[i][0]>maxx)maxx=verts[i][0];if(verts[i][1]<minz)minz=verts[i][1];if(verts[i][1]>maxz)maxz=verts[i][1];}
   x0=(int)floor(minx);x1=(int)ceil(maxx);y0=(int)floor(minz);y1=(int)ceil(maxz);
   if(x0<0)x0=0;if(y0<0)y0=0;if(x1>=DUKE_SIZE)x1=DUKE_SIZE-1;if(y1>=DUKE_SIZE)y1=DUKE_SIZE-1;
   for(y=y0;y<=y1;y++)for(x=x0;x<=x1;x++) {
    wa=Duke_Edge(b,c,x+0.5f,y+0.5f)/area;wb=Duke_Edge(c,a,x+0.5f,y+0.5f)/area;wc=1-wa-wb;
    if(wa<-0.00001f || wb<-0.00001f || wc<-0.00001f)continue;
    z=wa*a[2]+wb*b[2]+wc*c[2];index=y*DUKE_SIZE+x;if(z<duke_depth[index])continue;
    u=wa*uv[0][0]+wb*uv[1][0]+wc*uv[2][0];v=wa*uv[0][1]+wb*uv[1][1]+wc*uv[2][1];
    tx=(int)floor(u*meshes[m].width);ty=(int)floor(v*meshes[m].height);
    if(tx<0)tx=0;if(ty<0)ty=0;if(tx>=meshes[m].width)tx=meshes[m].width-1;if(ty>=meshes[m].height)ty=meshes[m].height-1;
    pixel=meshes[m].skin[ty*meshes[m].width+tx];if(pixel==255)continue;
    out->pixels[index]=mask?215:pixel;duke_depth[index]=z;visible=1;
   }
  }
 }
 return visible;
}
#endif
