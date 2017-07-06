/*
	Copyright 2017 Cisco

	Licensed under the Apache License, Version 2.0 (the "License");
	you may not use this file except in compliance with the License.
	You may obtain a copy of the License at

		http://www.apache.org/licenses/LICENSE-2.0

	Unless required by applicable law or agreed to in writing, software
	distributed under the License is distributed on an "AS IS" BASIS,
	WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
	See the License for the specific language governing permissions and
	limitations under the License.
*/
#ifndef	PVDID_UTILS_H
#define	PVDID_UTILS_H

#undef	true
#undef	false
#define	true	(1 == 1)
#define	false	(1 == 0)

#define	DLOG(args...)	\
	if (lFlagVerbose) {\
		fprintf(stderr, "pvdd : ");\
		fprintf(stderr, args);\
	}

#define	DIM(t)		(sizeof(t) / sizeof(t[0]))
#define	EQSTR(a,b)	(strcmp((a), (b)) == 0)

typedef	struct {
	int	MaxLength;
	int	Length;
	char	*String;
}	t_StringBuffer;

extern int getint(char *s, int *PtN);
extern void SBInit(t_StringBuffer *PtSB);
extern void SBUninit(t_StringBuffer *PtSB);
extern int SBAddString(t_StringBuffer *PtSB, char *fmt, ...);
extern char *Stringify(char *s);
extern char *JsonString(char *str);
extern char *JsonArray(int nStr, char **str);
extern char *GetIntStr(int n);

extern int lFlagVerbose;

#endif		/* PVDID_UTILS_H */

/* ex: set ts=8 noexpandtab wrap: */
