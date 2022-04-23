/**
 * @file fs.c  Baresip WebRTC demo -- filesystem functions
 *
 * Copyright (C) 2010 Alfred E. Heggestad
 */

#include <string.h>
#include <re.h>
#include <baresip.h>
#include "demo.h"


const char *fs_file_extension(const char *filename)
{
	const char *p;

	if (!filename)
		return NULL;

	p = strrchr(filename, '.');
	if (!p)
		return NULL;

	return p + 1;
}
