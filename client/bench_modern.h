/* Dedicated, bounded modern-effects plan. Uses the current output mode and
   the normal benchmark snapshot/log/repeat/recovery code. */
#define BENCH_MODERN (-17)
static const char *modern_bench_vars[8]={"gl_materials","gl_soft_particles","gl_smoke_lighting","gl_ssr","gl_ssgi","gl_volume_fog","gl_caustics","gl_water_scattering"};
static const char *modern_bench_labels[8]={"material response","soft smoke","coloured smoke","reflections","indirect light","volumetric fog","underwater caustics","underwater light shafts"};
static int ModernBenchCapable(int i)
{
 return Cvar_VariableValue(i==0?"bdgn_gl_materials_capable":i<3?"bdgn_gl_soft_particles_capable":"bdgn_gl_environment_capable")!=0;
}
static void ModernBenchBuild(void)
{
 int i,j; char label[128];
 BenchAddCase(BENCH_MODERN,0,"MODERN / reference (same output mode)");
 if(CL_SkyboxAvailable()) for(i=0;i<=10;i++) {
  Com_sprintf(label,sizeof(label),"MODERN / sky %i + matching light",i);
  BenchAddCase(BENCH_MODERN,1+i,label);
 }
 for(i=0;i<8;i++) if(ModernBenchCapable(i)) for(j=0;j<((i>=3 && i<=5 || i==7)?3:2);j++) {
  Com_sprintf(label,sizeof(label),"MODERN / %s %i",modern_bench_labels[i],j);
  BenchAddCase(BENCH_MODERN,20+i*3+j,label);
 }
 if(Cvar_VariableValue("bdgn_gl_soft_shadow_capable")) {
  BenchAddCase(BENCH_MODERN,110,"MODERN / native SMP off (combined high)");
  BenchAddCase(BENCH_MODERN,111,"MODERN / native SMP on (combined high)");
 }
 BenchAddCase(BENCH_MODERN,100,"MODERN / combined low");
 BenchAddCase(BENCH_MODERN,101,"MODERN / combined high");
}
static void ModernBenchApply(int choice)
{
 int i;
 for(i=0;i<8;i++) Cvar_SetValue((char *)modern_bench_vars[i],0);
 Cvar_SetValue("cl_skybox",0); Cvar_SetValue("gl_sky_lighting",1);
 Cvar_SetValue("gl_pixel_lighting",4); Cvar_SetValue("gl_dynamic",1); Cvar_SetValue("cl_weaponlights",1);
 Cvar_SetValue("gl_ztrick",0); Cvar_SetValue("r_fullbright",0); Cvar_SetValue("gl_fastworld",0);
 Cvar_SetValue("cl_hdsmoke",CL_FX3D_CvarAvailable("cl_hdsmoke")==1?1:0);
 if(choice>=1 && choice<=11) Cvar_SetValue("cl_skybox",choice-1);
 if(choice>=20 && choice<44) {
  i=(choice-20)/3; if(i<8 && ModernBenchCapable(i)) Cvar_SetValue((char *)modern_bench_vars[i],(choice-20)%3);
 }
 if(choice>=100) for(i=0;i<8;i++) if(ModernBenchCapable(i))
  Cvar_SetValue((char *)modern_bench_vars[i],(choice==101 || choice>=110) && (i>=3 && i<=5 || i==7)?2:1);
 if(choice>=110 && choice<=111) Cvar_SetValue("gl_smp",choice-110);
 Cvar_SetValue("r_profile",0); Cvar_SetValue("timedemo",1); Cvar_SetValue("paused",0); Cvar_Set("nextserver","");
 BenchGuards();
}
