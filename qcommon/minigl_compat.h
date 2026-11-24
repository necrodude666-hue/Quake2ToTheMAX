#ifndef BDGN_MINIGL_COMPAT_H
#define BDGN_MINIGL_COMPAT_H

static int BDGN_GLDriverIs(const char *driver, const char *name)
{
 const char *base=driver, *p;
 int length=(int)strlen(name);
 for(p=driver;*p;p++)
  if(*p=='/' || *p=='\\') base=p+1;
 return !Q_strncasecmp(base,name,length) &&
        (!base[length] || !Q_stricmp(base+length,".dll"));
}

/* Avoid weapon sprite texture uploads on the legacy 3dfx drivers. */
static int BDGN_GLUses3DWeapons(const char *driver, const char *target)
{
 if(BDGN_GLDriverIs(driver,"mtgl32")) driver=target;
 return BDGN_GLDriverIs(driver,"3dfxgl") ||
        BDGN_GLDriverIs(driver,"3dfxvgl") ||
        BDGN_GLDriverIs(driver,"Wicked3DFXgl");
}

#endif
