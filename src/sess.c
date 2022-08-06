
#include <string.h>
#include <re.h>
#include <baresip.h>
#include "demo.h"


void session_close(struct session *sess, int err)
{
	if (err)
		warning("demo: session '%s' closed (%m)\n", sess->id, err);
	else
		info("demo: session '%s' closed\n", sess->id);

	sess->pc = mem_deref(sess->pc);

	if (err) {
		http_ereply(sess->conn_pending, 500, "Session closed");
	}

	mem_deref(sess);
}
