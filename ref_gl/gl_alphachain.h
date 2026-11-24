/* gl_alphachain.h -- BDGN 3.56b: the list of translucent surfaces (water, glass) drawn by R_DrawAlphaSurfaces after the
opaque scene. The world walk and the brush models add to it; R_DrawAlphaSurfaces draws and empties it.

A surface can be in the list once (msurface_t.alphaframe = the list it is in). 3.55o-3.56a: the water reflection pass
added its surfaces and nothing emptied the list before the main view added its own; a surface in both made
texturechain loop back on itself and R_DrawAlphaSurfaces never ended (the game hung, "not responding"). Emptying the
list starts a new one, so the surfaces can be added again. Needs msurface_t with texturechain and alphaframe.
No engine code (test/alphachain_test.c). VS2005 C. */
#ifndef GL_ALPHACHAIN_H
#define GL_ALPHACHAIN_H

typedef struct
{
	msurface_t	*head;
	int			stamp;		/* which list: surfaces with alphaframe == stamp are in it */
} alphachain_t;

/* empty the list; the next one has a new stamp (never 0: surfaces start with alphaframe 0) */
static void R_AlphaChainClear (alphachain_t *c)
{
	c->head = NULL;
	c->stamp = c->stamp > 0 && c->stamp < 0x7fffffff ? c->stamp + 1 : 1;	/* no overflow past INT_MAX */
}

/* add s in front, unless it is already in this list */
static void R_AlphaChainAdd (alphachain_t *c, msurface_t *s)
{
	if (c->stamp <= 0)
		R_AlphaChainClear (c);
	if (s->alphaframe == c->stamp)
		return;
	s->alphaframe = c->stamp;
	s->texturechain = c->head;
	c->head = s;
}

#endif
