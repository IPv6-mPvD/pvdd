/*
 * INSERT PROPER HEADER HERE
 */

/*
 * Set of utilitarian routines, such as string manipulations, logging, etc.
 */

#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>

#include "pvdid-utils.h"

int	lFlagVerbose = false;

int	getint(char *s, int *PtN)
{
	int	n = 0;
	char	*pt;

	errno = 0;

	n = strtol(s, &pt, 10);

	if (errno == 0 && pt != s && *pt == '\0') {
		*PtN = n;
		return(0);
	}
	return(-1);
}

// SBInit : initialize a SB structure (StringBuffer, aka an on demand growing string)
void	SBInit(t_StringBuffer *SB)
{
	SB->Length = SB->MaxLength = 0;
	SB->String = NULL;
}

void	SBUninit(t_StringBuffer *SB)
{
	if (SB->String != NULL) {
		free(SB->String);
	}
	SBInit(SB);
}

// SBAddString : add a string to a string buffer
int SBAddString(t_StringBuffer *SB, char *fmt, ...)
{
	va_list	ap;
	int	n, r;

	if (SB->MaxLength == 0) {
		if ((SB->String = malloc(4096)) == NULL) {
			DLOG("memory overflow allocating string buffer\n");
			return(-1);
		}
		SB->MaxLength = 4096;
	}

	r = SB->MaxLength - 1 - SB->Length;

	va_start(ap, fmt);

	n = vsnprintf(&SB->String[SB->Length], r, fmt, ap) + 0;	// +1 for '\0'

	va_end(ap);

	// Do we need to extend the buffer ?
	if (n + 1 > r) {
		int NewSize = SB->Length + n;
		char *pt;

		if ((NewSize % 4096) != 0) {
			NewSize = NewSize / 4096 + 4096;
		}

		if ((pt = realloc(SB->String, NewSize)) == NULL) {
			DLOG("memory overflow reallocating string buffer\n");
			return(-1);
		}
		SB->String = pt;
		SB->MaxLength = NewSize;

		va_start(ap, fmt);

		if ((n = vsnprintf(&SB->String[SB->Length], r, fmt, ap)) != r) {
			DLOG("internal error (extending a buffer string)\n");
			va_end(ap);
			return(-1);
		}
		va_end(ap);
	}

	SB->Length += n;

	return(0);
}

// JsonString : convert a string in a JSON compatible string
// The function returns a static string and keeps this string valid for
// 2 consecutive calls
char	*JsonString(char *str)
{
	static	char		*lHexChars = "0123456789abcdef";
	static	int		lN = 0;
	static	unsigned char	*lStrings[2] = { NULL, NULL };

	unsigned char c;
	unsigned char *ustr = (unsigned char *) str;
	unsigned char *pt, *pt0;

	if (lStrings[lN] != NULL) {
		free(lStrings[lN]);
		lStrings[lN] = NULL;
	}

	if ((pt0 = pt = malloc(strlen(str) * 6 + 1)) == NULL) {	// worst case size
		return("<memory overflow>");	// FIXME : find a better return value
	}
	lStrings[lN] = pt0;

	while ((c = *ustr++) != '\0') {
		switch (c) {
			case '\b': case '\n': case '\r':
			case '\t': case '\f': case '"':
			case '\\': case '/':
				*pt++ = '\\';
				*pt++ = c == '\b' ? 'b' :
					c == '\n' ? 'n' :
					c == '\r' ? 'r' :
					c == '\t' ? 't' :
					c == '\f' ? 'f' :
					c == '"' ? '"' :
					c == '\\' ? '\\' :
					c == '/' ? '/' : c;
				break;
			default:
				if (c < ' ') {
					*pt++ = '\\';
					*pt++ = 'u';
					*pt++ = '0';
					*pt++ = '0';
					*pt++ = lHexChars[c >> 4];
					*pt++ = lHexChars[c & 15];
				}
				else {
					*pt++ = c;
				}
				break;
		}
	}
	*pt++ = '\0';

	lN = (lN + 1) % DIM(lStrings);

	return((char *) pt0);
}

// Stringify : return a string enclosed in ". The returned string is
// a static string, so only one call at a time is allowed
char	*Stringify(char *s)
{
	static	char	lS[1024];

	snprintf(lS, sizeof(lS) - 1, "\"%s\"", s);
	lS[sizeof(lS) - 1] = '\0';
	return(lS);
}

// JsonArray : convert an array of strings into its JSON string representation
// The returned string must be released by calling free()
char	*JsonArray(int nStr, char **str)
{
	int		i;
	t_StringBuffer	SB;

	SBInit(&SB);

	SBAddString(&SB, "[");
	for (i = 0; i < nStr; i++) {
		SBAddString(&SB, "\"");
		SBAddString(&SB, "%s", JsonString(str[i]));
		SBAddString(&SB, i == nStr - 1 ? "\"" : "\", ");
	}
	SBAddString(&SB, "]");

	return(SB.String);
}

/* GetIntStr : return a static string representation of an integer */
char	*GetIntStr(int n)
{
	static	char	lS[128];

	sprintf(lS, "%u", (unsigned int) n);

	return(lS);
}

/* ex: set ts=8 noexpandtab wrap: */
