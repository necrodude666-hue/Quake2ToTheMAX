/*
bdgn_winver.h -- BDGN 3.51p: Windows version name for bdgn_status and the benchmark log (from GetVersionEx values).
(Not the SDK's winver.h.) No Windows calls here, so it is tested on its own (test/winver_test.c).
platform: 0 Win32s, 1 Windows 95/98/ME, 2 NT line. On 9x the build number is in the low word of build.
Without a manifest, Windows 8.1, 10 and 11 report 6.2, so 6.2 is shown as "Windows 8 or later".
*/
#ifndef BDGN_WINVER_H
#define BDGN_WINVER_H

#include <stdio.h>
#include <string.h>

static void Sys_DescribeWindows (unsigned platform, unsigned major, unsigned minor, unsigned build, const char *csd,
								 char *out, int size)
{
	const char	*name = NULL;
	char		text[160], number[48];
	unsigned	b = platform == 1 ? (build & 0xffff) : build;

	if (size <= 0)
		return;
	if (platform == 1)
	{
		if (major == 4 && minor == 0) name = "Windows 95";
		else if (major == 4 && minor == 10) name = b >= 2222 ? "Windows 98 SE" : "Windows 98";
		else if (major == 4 && minor == 90) name = "Windows ME";
		else name = "Windows 9x";
	}
	else if (platform == 2)
	{
		if (major == 4) name = "Windows NT 4.0";
		else if (major == 5 && minor == 0) name = "Windows 2000";
		else if (major == 5 && minor == 1) name = "Windows XP";
		else if (major == 5 && minor == 2) name = "Windows Server 2003 / XP x64";
		else if (major == 6 && minor == 0) name = "Windows Vista";
		else if (major == 6 && minor == 1) name = "Windows 7";
		else if (major == 6 && minor >= 2) name = "Windows 8 or later";
		else if (major == 10) name = "Windows 10 or 11";
	}
	else if (platform == 0)
		name = "Win32s";
	sprintf (number, "%u.%u.%u", major, minor, b);
	if (name)
		sprintf (text, "%.60s (%s)", name, number);
	else
		sprintf (text, "%s %s", platform == 2 ? "Windows NT" : "unknown Windows", number);
	if (csd)
	{
		while (*csd == ' ')
			csd++;
		if (*csd)
		{
			strcat (text, " ");
			strncat (text, csd, sizeof(text) - strlen (text) - 1);
		}
	}
	strncpy (out, text, size - 1);
	out[size - 1] = 0;
}

#endif
