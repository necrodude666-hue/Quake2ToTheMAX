/* Native system-OpenGL SMP. Workers do CPU pose interpolation only.
   The window/context and EVERY OpenGL call stay on the rendering thread.
   Included once, after r_avertexnormals, by gl_mesh.c. GPL-2.0-or-later. */
#define SMP_POSES 128
#define SMP_BYTES (4U*1024U*1024U)
typedef struct {
 const entity_t *entity;
 const dmdl_t *header;
 const dtrivertx_t *v,*ov;
 float move[3],front[3],back[3],lerp;
 int frame,oldframe,count;
 float (*xyz)[4],(*normal)[3];
} smp_pose_t;
static smp_pose_t smp_poses[SMP_POSES];
static int smp_count;
static int smp_stamp=-1,smp_reused;
static byte *smp_memory;
static cvar_t *smp_enabled;
#ifdef _WIN32
typedef struct { HANDLE thread,start,done; } smp_worker_t;
static smp_worker_t smp_workers[3];
static int smp_threads,smp_running;
static volatile LONG smp_next,smp_stop;
static volatile LONG smp_worker_jobs;

static void SMP_Calculate(smp_pose_t *p)
{
 int i,k; float front=1-p->lerp;
 for(i=0;i<p->count;i++) {
  const float *n=r_avertexnormals[p->v[i].lightnormalindex];
  const float *old=r_avertexnormals[p->ov[i].lightnormalindex];
  for(k=0;k<3;k++) {
   p->xyz[i][k]=p->move[k]+p->ov[i].v[k]*p->back[k]+p->v[i].v[k]*p->front[k];
   p->normal[i][k]=n[k]*front+old[k]*p->lerp;
  }
  p->xyz[i][3]=0;
 }
}
static void SMP_Help(int worker)
{
 LONG i;
 while((i=InterlockedIncrement(&smp_next)-1)<smp_count) {
  SMP_Calculate(&smp_poses[i]); if(worker) InterlockedIncrement(&smp_worker_jobs);
 }
}
static DWORD WINAPI SMP_Work(void *arg)
{
 smp_worker_t *w=(smp_worker_t *)arg;
 for(;;) {
  WaitForSingleObject(w->start,INFINITE);
  if(smp_stop) break; /* Event hand-off provides the memory barrier; Win95 API set. */
  SMP_Help(1); SetEvent(w->done);
 }
 return 0;
}
#endif
void R_SMPFinish(void)
{
#ifdef _WIN32
 int i;
 if(!smp_running) return;
 SMP_Help(0);
 for(i=0;i<smp_threads;i++) WaitForSingleObject(smp_workers[i].done,INFINITE);
 smp_running=0;
#endif
}
void R_SMPShutdown(void)
{
#ifdef _WIN32
 int i;
 R_SMPFinish(); InterlockedExchange(&smp_stop,1);
 for(i=0;i<smp_threads;i++) SetEvent(smp_workers[i].start);
 for(i=0;i<smp_threads;i++) {
  WaitForSingleObject(smp_workers[i].thread,INFINITE);
  CloseHandle(smp_workers[i].thread); CloseHandle(smp_workers[i].start); CloseHandle(smp_workers[i].done);
 }
 memset(smp_workers,0,sizeof(smp_workers)); smp_threads=0;
#endif
 free(smp_memory); smp_memory=NULL; smp_count=0; smp_stamp=-1;
 ri.Cmd_RemoveCommand("gl_smp_stats");
 ri.Cvar_SetValue("bdgn_gl_smp_workers",0);
}
static void R_SMPStats(void)
{
#ifdef _WIN32
 R_SMPFinish();
 ri.Con_Printf(PRINT_ALL,"Native GL SMP: %i workers, %i prepared poses, %ld worker jobs, %i pose reuses; 4 MiB maximum\n",smp_threads,smp_count,smp_worker_jobs,smp_reused);
#else
 ri.Con_Printf(PRINT_ALL,"Native GL SMP is a Windows rendering path\n");
#endif
}
void R_SMPInit(void)
{
 smp_enabled=ri.Cvar_Get("gl_smp","0",CVAR_ARCHIVE);
 ri.Cvar_SetValue("bdgn_gl_smp_workers",0);
 ri.Cmd_AddCommand("gl_smp_stats",R_SMPStats);
}
void R_SMPPrepare(void)
{
#ifdef _WIN32
 SYSTEM_INFO info; int i,j,k; unsigned used=0;
 R_SMPFinish(); smp_count=0; smp_stamp=r_framecount; smp_reused=0; smp_worker_jobs=0;
 /* CPU gate precedes allocation/thread creation, even on a modern GPU. */
 if(smp_enabled && !smp_enabled->value && smp_threads) { R_SMPShutdown(); ri.Cmd_AddCommand("gl_smp_stats",R_SMPStats); }
 if(!smp_enabled || !smp_enabled->value || !gl_soft_shadow_capable || gl_reflect_pass ||
    (r_newrefdef.rdflags&RDF_NOWORLDMODEL)) return;
 if(!smp_threads) {
  GetSystemInfo(&info); if(info.dwNumberOfProcessors<2) return;
  smp_memory=(byte *)malloc(SMP_BYTES); if(!smp_memory) return;
  InterlockedExchange(&smp_stop,0);
  for(i=0;i<3 && i<(int)info.dwNumberOfProcessors-1;i++) {
   smp_worker_t *w=&smp_workers[i];
   w->start=CreateEvent(NULL,FALSE,FALSE,NULL); w->done=CreateEvent(NULL,TRUE,TRUE,NULL);
   if(w->start && w->done) w->thread=CreateThread(NULL,0,SMP_Work,w,0,NULL);
   if(!w->thread) { if(w->start) CloseHandle(w->start); if(w->done) CloseHandle(w->done); memset(w,0,sizeof(*w)); break; }
   smp_threads++;
  }
  if(!smp_threads) { free(smp_memory); smp_memory=NULL; return; }
  ri.Cvar_SetValue("bdgn_gl_smp_workers",smp_threads);
  ri.Con_Printf(PRINT_ALL,"Native OpenGL SMP: %i CPU workers; OpenGL context stays on main thread\n",smp_threads);
 }
 for(i=0;i<r_newrefdef.num_entities && smp_count<SMP_POSES;i++) {
  entity_t *e=&r_newrefdef.entities[i]; model_t *m=(model_t *)e->model;
  const dmdl_t *h; const daliasframe_t *f,*old; smp_pose_t *p;
  vec3_t delta,axis[3],move; float back,front; unsigned bytes;
  if(!m || m->type!=mod_alias || !m->extradata ||
     (e->flags&(RF_WEAPONMODEL|RF_SHELL_RED|RF_SHELL_GREEN|RF_SHELL_BLUE|RF_SHELL_DOUBLE|RF_SHELL_HALF_DAM|RF_FXMESH|RF_PUFFS))) continue;
  h=(const dmdl_t *)m->extradata;
  if(h->num_xyz<64 || h->num_xyz>MAX_VERTS || e->frame<0 || e->frame>=h->num_frames || e->oldframe<0 || e->oldframe>=h->num_frames) continue;
  bytes=(unsigned)h->num_xyz*7*sizeof(float); if(bytes>SMP_BYTES-used) continue;
  back=r_lerpmodels->value?e->backlerp:0;
  VectorSubtract(e->origin,r_origin,delta);
  if(gl_lowdetail->value>0 && DotProduct(delta,delta)>gl_lowdetail->value*gl_lowdetail->value) back=0;
  front=1-back;
  f=(const daliasframe_t *)((const byte *)h+h->ofs_frames+e->frame*h->framesize);
  old=(const daliasframe_t *)((const byte *)h+h->ofs_frames+e->oldframe*h->framesize);
  /* FX models encode other data in normal indices. Never send them here. */
  for(j=0;j<h->num_xyz;j++) if(f->verts[j].lightnormalindex>=162 || old->verts[j].lightnormalindex>=162) break;
  if(j<h->num_xyz) continue;
  VectorSubtract(e->oldorigin,e->origin,delta); R_AngleVectors(e->angles,axis[0],axis[1],axis[2]);
  move[0]=DotProduct(delta,axis[0]); move[1]=-DotProduct(delta,axis[1]); move[2]=DotProduct(delta,axis[2]);
  p=&smp_poses[smp_count++]; p->entity=e; p->header=h; p->frame=e->frame; p->oldframe=e->oldframe; p->lerp=back;
  p->v=f->verts; p->ov=old->verts; p->count=h->num_xyz;
  p->xyz=(float (*)[4])(smp_memory+used); p->normal=(float (*)[3])(p->xyz+h->num_xyz); used+=bytes;
  for(k=0;k<3;k++) { p->move[k]=back*(move[k]+old->translate[k])+front*f->translate[k]; p->front[k]=front*f->scale[k]; p->back[k]=back*old->scale[k]; }
 }
 if(!smp_count) return;
 InterlockedExchange(&smp_next,0); smp_running=1;
 for(i=0;i<smp_threads;i++) { ResetEvent(smp_workers[i].done); SetEvent(smp_workers[i].start); }
#endif
}
static const smp_pose_t *SMP_Pose(const entity_t *e,const dmdl_t *h,float back)
{
 int i;
 if(gl_reflect_pass || smp_stamp!=r_framecount) return NULL;
 for(i=0;i<smp_count;i++) if(smp_poses[i].entity==e && smp_poses[i].header==h && smp_poses[i].frame==e->frame &&
    smp_poses[i].oldframe==e->oldframe && smp_poses[i].lerp==back) { R_SMPFinish(); smp_reused++; return &smp_poses[i]; }
 return NULL;
}
