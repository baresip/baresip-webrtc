/**
 * @file util.c  Baresip WebRTC demo -- utility functions
 *
 * Copyright (C) 2010 Alfred E. Heggestad
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_IO_H
#include <io.h>
#endif
#include <string.h>
#include <re.h>
#include <baresip.h>
#include "demo.h"


int load_file(struct mbuf *mb, const char *filename)
{
	int err = 0, fd = open(filename, O_RDONLY);
	if (fd < 0)
		return errno;

	for (;;) {
		uint8_t buf[1024];

		const ssize_t n = read(fd, (void *)buf, sizeof(buf));
		if (n < 0) {
			err = errno;
			break;
		}
		else if (n == 0)
			break;

		err |= mbuf_write_mem(mb, buf, n);
	}

	(void)close(fd);

	return err;
}


const char *file_extension(const char *filename)
{
	const char *p;

	if (!filename)
		return NULL;

	p = strrchr(filename, '.');
	if (!p)
		return NULL;

	return p + 1;
}
