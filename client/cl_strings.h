/* cl_strings.h -- BDGN 3.51ar: bounded copies of strings the server sends to the client.
   id used strcpy for these (a long config string near the end of the table, a long map name or a long player model
   name overran the destination). No engine code needed (test/cl_strings_test.c). VS2005 C. */
#ifndef CL_STRINGS_H
#define CL_STRINGS_H

#include <string.h>

/* at most size-1 characters, always terminated */
static void CL_CopyString (char *dst, int size, const char *src)
{
	int n;

	if (size <= 0)
		return;
	n = (int)strlen (src);
	if (n > size - 1)
		n = size - 1;
	memcpy (dst, src, n);
	dst[n] = 0;
}

/* config string into slot index of a table of nslots slots of slotsize bytes. A long string may run on into the
   following slots, as in id's code (the status bar layout is one such string), but never past the table. */
static void CL_StoreConfigString (char *table, int index, int slotsize, int nslots, const char *s)
{
	if (index < 0 || index >= nslots)
		return;
	CL_CopyString (table + index * slotsize, (nslots - index) * slotsize, s);
}

/* "maps/<name>.bsp" -> "<name>"; any other path is copied without the parts it lacks */
static void CL_MapName (char *out, int size, const char *model)
{
	int n;

	if (!strncmp (model, "maps/", 5))
		model += 5;
	CL_CopyString (out, size, model);
	n = (int)strlen (out);
	if (n >= 4 && !strcmp (out + n - 4, ".bsp"))
		out[n - 4] = 0;
}

#endif
