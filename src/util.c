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


const char *extension_to_mimetype(const char *ext)
{
	if (0 == str_casecmp(ext, "html")) return "text/html";
	if (0 == str_casecmp(ext, "js"))   return "text/javascript";

	return "application/octet-stream";  /* default */
}


int reply_fmt(struct http_conn *conn, const char *ctype, const char *fmt, ...)
{
	char *buf = NULL;
	va_list ap;
	int err;

	if (!conn || !ctype || !fmt)
		return EINVAL;

	va_start(ap, fmt);
	err = re_vsdprintf(&buf, fmt, ap);
	va_end(ap);

	if (err)
		return err;

	info("demo: reply: %s\n", ctype);

	err = http_reply(conn, 200, "OK",
			 "Content-Type: %s\r\n"
			 "Content-Length: %zu\r\n"
			 "Access-Control-Allow-Origin: *\r\n"
			 "\r\n"
			 "%s",
			 ctype, str_len(buf), buf);

	mem_deref(buf);

	return err;
}
