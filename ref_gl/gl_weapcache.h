/* gl_weapcache.h -- BDGN 3.52: which 2D weapon pictures are kept as textures (cl_weaponsprites).

3.51att-3.51bat kept 32 textures of 256x256 and replaced the least recently used one. A weapon's idle loop has
18-44 pictures (game/p_weapon.c: blaster 44, grenade launcher 43, machinegun and super shotgun 40, rocket launcher
and railgun 38). With a loop longer than the cache, least-recently-used replacement throws out exactly the picture
that is needed next, so every pose change decoded, converted and uploaded a picture again (bench 7 Nov, K6-III +
GeForce2: 2D weapon at 12.5 poses/s 12 % slower than the 3D weapon, about 10 ms per pose change).

Now:
- a memory budget (gl_weapon_texmem, MB) instead of a count;
- each texture is as tall as the weapon needs (power of two, 8-256): most weapons are 94-125 pixels tall, so 256x128,
  half the memory of 256x256;
- replacement: first pictures of other weapons (their idle picture last: it is preloaded at map start so that a
  weapon switch shows at once), then, within the current weapon, the second newest picture: the cache keeps what it
  has instead of cycling, so a loop that does not fit is still mostly cached. A loop that fits is completely cached
  after its first pass.
The caller owns the textures: create = 1 means (re)specify the slot's texture at slot height; free_tex(slot) is called
when a slot's texture is given up to stay within the budget. No engine code (test/weapcache_test.c). VS2005 C. */
#ifndef GL_WEAPCACHE_H
#define GL_WEAPCACHE_H

#define WSC_SLOTS	128

typedef struct
{
	int			valid;			/* holds a picture */
	int			bank, frame, idle;
	int			texh;			/* height of the texture storage this slot holds, 0 = none */
	unsigned	used;
} wsc_slot_t;

typedef struct
{
	wsc_slot_t	s[WSC_SLOTS];
	unsigned	clock;
	int			bytes;			/* texture storage held by all slots */
} wsc_t;

/* texture height for a picture h pixels tall: power of two, 8-256 */
static int WSC_TexHeight (int h)
{
	int t = 8;

	while (t < h && t < 256)
		t <<= 1;
	return t;
}

/* bytes of a 256 x texh RGBA texture */
static int WSC_TexBytes (int texh)
{
	return 256 * texh * 4;
}

/* slot holding (bank, frame), or -1; a hit counts as use */
static int WSC_Find (wsc_t *c, int bank, int frame)
{
	int i;

	for (i = 0 ; i < WSC_SLOTS ; i++)
		if (c->s[i].valid && c->s[i].bank == bank && c->s[i].frame == frame)
		{
			c->s[i].used = ++c->clock;
			return i;
		}
	return -1;
}

/* picture to replace, or -1 if the cache is empty */
static int WSC_Victim (const wsc_t *c, int bank)
{
	int i, pass, best = -1, newest = -1, second = -1;

	for (pass = 0 ; pass < 2 ; pass++)
	{	/* other weapons: pictures of animations first, idle pictures last; least recently used first */
		for (i = 0 ; i < WSC_SLOTS ; i++)
			if (c->s[i].valid && c->s[i].bank != bank && c->s[i].idle == pass
				&& (best < 0 || c->s[i].used < c->s[best].used))
				best = i;
		if (best >= 0)
			return best;
	}
	/* the current weapon: the second newest picture (the newest one is on screen now) */
	for (i = 0 ; i < WSC_SLOTS ; i++)
	{
		if (!c->s[i].valid)
			continue;
		if (newest < 0 || c->s[i].used > c->s[newest].used)
		{
			second = newest;
			newest = i;
		}
		else if (second < 0 || c->s[i].used > c->s[second].used)
			second = i;
	}
	return second >= 0 ? second : newest;
}

/* a slot for a picture that is not cached, h pixels tall */
static int WSC_Place (wsc_t *c, int bank, int frame, int idle, int h, int budget, void (*free_tex) (int slot), int *create)
{
	int texh = WSC_TexHeight (h), need = WSC_TexBytes (texh), i, slot = -1, guard;

	*create = 0;
	/* gl_weapon_texmem was lowered while playing: give up textures until the cache is within it (Codex review of
	   3.52). Only when over the budget, so a full cache still reuses the texture of the picture it replaces. */
	for (guard = 0 ; guard < 2 * WSC_SLOTS && c->bytes > budget ; guard++)
	{
		for (i = 0 ; i < WSC_SLOTS ; i++)
			if (!c->s[i].valid && c->s[i].texh)
				break;
		if (i == WSC_SLOTS)
		{
			i = WSC_Victim (c, bank);
			if (i < 0)
				break;
			c->s[i].valid = 0;
		}
		c->bytes -= WSC_TexBytes (c->s[i].texh);
		c->s[i].texh = 0;
		if (free_tex)
			free_tex (i);
	}
	for (guard = 0 ; guard < 4 * WSC_SLOTS && slot < 0 ; guard++)
	{
		/* an empty slot that already holds a texture of this height */
		for (i = 0 ; i < WSC_SLOTS && slot < 0 ; i++)
			if (!c->s[i].valid && c->s[i].texh == texh)
				slot = i;
		if (slot >= 0)
			break;
		/* room in the budget: an empty slot without a texture */
		if (c->bytes + need <= budget)
			for (i = 0 ; i < WSC_SLOTS && slot < 0 ; i++)
				if (!c->s[i].valid && !c->s[i].texh)
					slot = i;
		if (slot >= 0)
		{
			*create = 1;
			break;
		}
		/* give up the texture of an empty slot of another height */
		for (i = 0 ; i < WSC_SLOTS ; i++)
			if (!c->s[i].valid && c->s[i].texh)
				break;
		if (i < WSC_SLOTS)
		{
			c->bytes -= WSC_TexBytes (c->s[i].texh);
			c->s[i].texh = 0;
			if (free_tex)
				free_tex (i);
			continue;
		}
		/* replace a picture (its slot becomes empty and is reused on the next pass) */
		i = WSC_Victim (c, bank);
		if (i < 0)
			break;
		c->s[i].valid = 0;
	}
	if (slot < 0)
	{	/* the budget is smaller than one picture: show it anyway, in one texture */
		for (i = 0 ; i < WSC_SLOTS && slot < 0 ; i++)
			if (!c->s[i].valid && !c->s[i].texh)
				slot = i;
		if (slot < 0)
			return -1;
		*create = 1;
	}
	if (*create)
		c->bytes += need;
	c->s[slot].valid = 1;
	c->s[slot].bank = bank;
	c->s[slot].frame = frame;
	c->s[slot].idle = idle != 0;
	c->s[slot].texh = texh;
	c->s[slot].used = ++c->clock;
	return slot;
}

#endif
