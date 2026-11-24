/*
fs_packfind.h -- PAK directory lookup (BDGN 3.51p, index for every PAK 3.51ar).

Every lookup returns what id's linear search returns: the first entry in the directory whose name is equal ignoring
the case of A-Z, or -1. It just gets there with a binary search:
- a directory that is strictly sorted (FS_PackSorted, checked once at load; bdgn_sprites.pak, bdgn_anim.pak, pak4.pak,
  pak5.pak) is searched directly;
- any other directory (id's pak0/pak1, the low quality packs, repacked or hand-made paks, duplicate names) gets an
  index at load (FS_PackBuildIndex): its entry numbers sorted by name, equal names by position, so the first of equal
  names is found first. 3.51p searched those linearly: about 3600 name compares for a file in id's pak0 after pak1,
  and every directory in full for a file that is not in a pak. Measured with the real directories on the build PC:
  24 us per lookup before; see REPORT_v3.51ar.md for the numbers after and the K6 estimate.

Order: FS_PackNameCmp, A-Z folded to a-z, then unsigned bytes. Not Q_strcasecmp: id's Q_strncasecmp returns only 0
(equal) or -1 (different), it never says which name comes first (3.51p's first build: "Couldn't load
pics/colormap.pcx"). Equality is the same as Q_strcasecmp's (both fold only ASCII letters).

Include after packfile_t is defined (qcommon/files.c). Needs qsort (stdlib.h). No other engine code
(test/pak_find_test.c links the real game/q_shared.c and compares every lookup with id's linear search).
*/
#ifndef FS_PACKFIND_H
#define FS_PACKFIND_H

#include <stdlib.h>

/* <0, 0, >0 like strcmp, case-insensitive for A-Z */
static int FS_PackNameCmp (const char *a, const char *b)
{
	int c1, c2;

	do
	{
		c1 = (unsigned char)*a++;
		c2 = (unsigned char)*b++;
		if (c1 >= 'A' && c1 <= 'Z')
			c1 += 'a' - 'A';
		if (c2 >= 'A' && c2 <= 'Z')
			c2 += 'a' - 'A';
		if (c1 != c2)
			return c1 - c2;
	} while (c1);
	return 0;
}

/* 1 = names strictly increasing (no index needed) */
static int FS_PackSorted (const packfile_t *files, int numfiles)
{
	int i;

	for (i = 1 ; i < numfiles ; i++)
		if (FS_PackNameCmp (files[i - 1].name, files[i].name) >= 0)
			return 0;
	return 1;
}

static const packfile_t *fs_index_files;	/* qsort has no context argument; loading is single-threaded */

static int FS_PackIndexCmp (const void *a, const void *b)
{
	int ia = *(const int *)a, ib = *(const int *)b;
	int c = FS_PackNameCmp (fs_index_files[ia].name, fs_index_files[ib].name);

	if (c)
		return c;
	return ia - ib;		/* equal names: directory order, so the first one comes first */
}

/* index[0..numfiles-1] = entry numbers sorted by (name, position) */
static void FS_PackBuildIndex (const packfile_t *files, int numfiles, int *index)
{
	int i;

	for (i = 0 ; i < numfiles ; i++)
		index[i] = i;
	fs_index_files = files;
	if (numfiles > 1)
		qsort (index, numfiles, sizeof(int), FS_PackIndexCmp);
	fs_index_files = NULL;
}

/* entry number of name, or -1. index = NULL for a sorted directory, else the one from FS_PackBuildIndex */
static int FS_PackFind (const packfile_t *files, int numfiles, const int *index, const char *name)
{
	int low = 0, high = numfiles, mid, entry;

	/* first position whose name is not below the wanted one */
	while (low < high)
	{
		mid = low + (high - low) / 2;
		entry = index ? index[mid] : mid;
		if (FS_PackNameCmp (files[entry].name, name) < 0)
			low = mid + 1;
		else
			high = mid;
	}
	if (low >= numfiles)
		return -1;
	entry = index ? index[low] : low;
	return FS_PackNameCmp (files[entry].name, name) ? -1 : entry;
}

#endif
