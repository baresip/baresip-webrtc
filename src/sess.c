
#include <string.h>
#include <re.h>
#include <baresip.h>
#include "demo.h"


struct session *session_lookup(const struct list *sessl,
			       const struct http_msg *msg)
{
	const struct http_hdr *hdr;

	hdr = http_msg_xhdr(msg, "Session-ID");
	if (!hdr) {
		warning("demo: no Session-ID header\n");
		return NULL;
	}

	for (struct le *le = sessl->head; le; le = le->next) {

		struct session *sess = le->data;

		if (0 == pl_strcasecmp(&hdr->val, sess->id))
			return sess;
	}

	warning("demo: session not found (%r)\n", &hdr->val);

	return NULL;
}


int session_handle_ice_candidate(struct session *sess, const struct odict *od)
{
	const char *cand, *mid;
	struct pl pl_cand;
	char *cand2 = NULL;
	int err;

	cand = odict_string(od, "candidate");
	mid  = odict_string(od, "sdpMid");
	if (!cand || !mid) {
		warning("demo: candidate: missing 'candidate' or 'mid'\n");
		return EPROTO;
	}

	err = re_regex(cand, str_len(cand), "candidate:[^]+", &pl_cand);
	if (err)
		return err;

	pl_strdup(&cand2, &pl_cand);

	peerconnection_add_ice_candidate(sess->pc, cand2, mid);

	mem_deref(cand2);

	return 0;
}


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
