/**
 * @file demo.c  Baresip WebRTC demo
 *
 * Copyright (C) 2010 Alfred E. Heggestad
 */

#include <string.h>
#include <re.h>
#include <baresip.h>
#include "demo.h"


enum {
	HTTP_PORT  = 9000,
	HTTPS_PORT = 9001
};


struct session {
	struct le le;
	struct peer_connection *pc;
	struct http_conn *conn_pending;
	char *id;
};


static struct demo {
	struct list sessl;
	uint32_t session_counter;
	const struct mnat *mnat;
	const struct menc *menc;
} demo;


static struct http_sock *httpsock;
static struct http_sock *httpssock;
static struct rtc_configuration pc_config = {
	.offerer = true
};


static void destructor(void *data)
{
	struct session *sess = data;

	list_unlink(&sess->le);
	mem_deref(sess->conn_pending);
	mem_deref(sess->pc);
	mem_deref(sess->id);
}


static void session_close(struct session *sess, int err)
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


static struct session *session_lookup(const struct http_msg *msg)
{
	const struct http_hdr *hdr;

	hdr = http_msg_xhdr(msg, "Session-ID");
	if (!hdr) {
		warning("demo: no Session-ID header\n");
		return NULL;
	}

	for (struct le *le = demo.sessl.head; le; le = le->next) {

		struct session *sess = le->data;

		if (0 == pl_strcasecmp(&hdr->val, sess->id))
			return sess;
	}

	warning("demo: session not found (%r)\n", &hdr->val);

	return NULL;
}


static void peerconnection_gather_handler(void *arg)
{
	struct session *sess = arg;
	struct mbuf *mb_sdp = NULL;
	enum sdp_type type;
	int err;

	switch (peerconnection_signaling(sess->pc)) {

	case SS_STABLE:
		type = SDP_OFFER;
		break;

	case SS_HAVE_LOCAL_OFFER:
		warning("illegal state\n");
		type = SDP_OFFER;
		break;

	case SS_HAVE_REMOTE_OFFER:
		type = SDP_ANSWER;
		break;
	}

	info("demo: session gathered -- send sdp '%s'\n", sdptype_name(type));

	if (type == SDP_OFFER)
		err = peerconnection_create_offer(sess->pc, &mb_sdp);
	else
		err = peerconnection_create_answer(sess->pc, &mb_sdp);
	if (err)
		goto out;

	err = http_reply_descr(sess->conn_pending, sess->id, type, mb_sdp);
	if (err) {
		warning("demo: reply error: %m\n", err);
		goto out;
	}

	if (type == SDP_ANSWER) {

		err = peerconnection_start_ice(sess->pc);
		if (err) {
			warning("demo: failed to start ice (%m)\n", err);
			goto out;
		}
	}

 out:
	mem_deref(mb_sdp);

	if (err)
		session_close(sess, err);
}


static void peerconnection_estab_handler(struct media_track *media, void *arg)
{
	struct session *sess = arg;
	int err = 0;

	info("demo: stream established: '%s'\n",
	     media_kind_name(mediatrack_kind(media)));

	switch (mediatrack_kind(media)) {

	case MEDIA_KIND_AUDIO:
		err = mediatrack_start_audio(media, baresip_ausrcl(),
					     baresip_aufiltl());
		if (err) {
			warning("demo: could not start audio (%m)\n", err);
		}
		break;

	case MEDIA_KIND_VIDEO:
		err = mediatrack_start_video(media);
		if (err) {
			warning("demo: could not start video (%m)\n", err);
		}
		break;

	default:
		break;
	}

	if (err) {
		session_close(sess, err);
		return;
	}

	stream_enable(media_get_stream(media), true);
}


static void peerconnection_close_handler(int err, void *arg)
{
	struct session *sess = arg;

	warning("demo: session closed (%m)\n", err);

	session_close(sess, err);
}


static int session_new(struct session **sessp)
{
	const struct config *config = conf_config();
	struct session *sess;
	int err;

	info("demo: create session\n");

	sess = mem_zalloc(sizeof(*sess), destructor);
	if (!sess)
		return ENOMEM;

	/* generate a unique session id */
	re_sdprintf(&sess->id, "%u", ++demo.session_counter);

	/* create a new session object, send SDP to it */
	err = peerconnection_new(&sess->pc, &pc_config, demo.mnat, demo.menc,
				 peerconnection_gather_handler,
				 peerconnection_estab_handler,
				 peerconnection_close_handler, sess);
	if (err) {
		warning("demo: session alloc failed (%m)\n", err);
		goto out;
	}

	err = peerconnection_add_audio_track(sess->pc, config,
					     baresip_aucodecl());
	if (err) {
		warning("demo: add_audio failed (%m)\n", err);
		goto out;
	}

	err = peerconnection_add_video_track(sess->pc, config,
					     baresip_vidcodecl());
	if (err) {
		warning("demo: add_video failed (%m)\n", err);
		goto out;
	}

	list_append(&demo.sessl, &sess->le, sess);

 out:
	if (err)
		mem_deref(sess);
	else if (sessp)
		*sessp = sess;

	return err;
}


static int handle_post_sdp(struct session *sess, const struct http_msg *msg)
{
	struct session_description sd = {-1, NULL};
	int err = 0;

	info("demo: handle POST sdp: content is '%r/%r'\n",
	     &msg->ctyp.type, &msg->ctyp.subtype);

	err = session_description_decode(&sd, msg->mb);
	if (err)
		return err;

	err = peerconnection_set_remote_descr(sess->pc, &sd);
	if (err) {
		warning("demo: set remote descr error"
			" (%m)\n", err);
		goto out;
	}

	if (sd.type == SDP_ANSWER) {

		err = peerconnection_start_ice(sess->pc);
		if (err) {
			warning("demo: failed to start ice"
				" (%m)\n", err);
			goto out;
		}
	}

out:
	session_description_reset(&sd);

	return err;
}


static int handle_ice_candidate(struct session *sess, const struct odict *od)
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


static void handle_get(struct http_conn *conn, const struct pl *path)
{
	const char *ext, *mime;
	struct mbuf *mb = NULL;
	char *buf = NULL;
	int err;

	err = re_sdprintf(&buf, "./www%r", path);
	if (err)
		goto out;

	err = conf_loadfile(&mb, buf);
	if (err) {
		info("demo: not found: %s\n", buf);
		http_ereply(conn, 404, "Not Found");
		goto out;
	}

	ext = fs_file_extension(buf);
	mime = http_extension_to_mimetype(ext);

	info("demo: loaded file '%s', %zu bytes (%s)\n", buf, mb->end, mime);

	http_reply(conn, 200, "OK",
		   "Content-Type: %s;charset=UTF-8\r\n"
		   "Content-Length: %zu\r\n"
		   "Access-Control-Allow-Origin: *\r\n"
		   "\r\n"
		   "%b",
		   mime,
		   mb->end,
		   mb->buf, mb->end);

 out:
	mem_deref(mb);
	mem_deref(buf);
}


static void http_req_handler(struct http_conn *conn,
			     const struct http_msg *msg, void *arg)
{
	struct pl path = PL("/index.html");
	struct session *sess;
	struct odict *od = NULL;
	int err = 0;
	(void)arg;

	info("demo: request: met=%r, path=%r, prm=%r\n",
	     &msg->met, &msg->path, &msg->prm);

	if (msg->path.l > 1)
		path = msg->path;

	if (0 == pl_strcasecmp(&msg->met, "GET")) {

		handle_get(conn, &path);
	}
	else if (0 == pl_strcasecmp(&msg->met, "POST") &&
		 0 == pl_strcasecmp(&msg->path, "/connect")) {

		err = session_new(&sess);
		if (err)
			goto out;

		if (pc_config.offerer) {
			/* async reply */
			mem_deref(sess->conn_pending);
			sess->conn_pending = mem_ref(conn);
		}
		else {
			/* sync reply */
			http_reply(conn, 201, "Created",
				   "Content-Length: 0\r\n"
				   "Access-Control-Allow-Origin: *\r\n"
				   "Session-ID: %s\r\n"
				   "\r\n", sess->id);
		}
	}
	else if (0 == pl_strcasecmp(&msg->met, "PUT") &&
		 0 == pl_strcasecmp(&msg->path, "/sdp")) {

		sess = session_lookup(msg);
		if (sess) {
			if (msg->clen &&
			    msg_ctype_cmp(&msg->ctyp, "application", "json")) {
				err = handle_post_sdp(sess, msg);
				if (err)
					goto out;
			}

			if (pc_config.offerer) {

				/* sync reply */
				http_reply(conn, 200, "OK",
					   "Content-Length: 0\r\n"
					   "Access-Control-Allow-Origin: *\r\n"
					   "\r\n");
			}
			else {
				/* async reply */
				mem_deref(sess->conn_pending);
				sess->conn_pending = mem_ref(conn);
			}
		}
		else {
			http_ereply(conn, 404, "Session Not Found");
			return;
		}
	}
	else if (0 == pl_strcasecmp(&msg->met, "PATCH")) {

		sess = session_lookup(msg);
		if (sess) {
			enum {HASH_SIZE = 4, MAX_DEPTH = 2};

			/* XXX: move json decode further up */
			err = json_decode_odict(&od, HASH_SIZE,
						(char *)mbuf_buf(msg->mb),
						mbuf_get_left(msg->mb),
						MAX_DEPTH);
			if (err) {
				warning("demo: candidate:"
					" could not decode json (%m)\n", err);
				goto out;
			}

			handle_ice_candidate(sess, od);

			/* sync reply */
			http_reply(conn, 204, "No Content",
				   "Content-Length: 0\r\n"
				   "Access-Control-Allow-Origin: *\r\n"
				   "\r\n");
		}
		else {
			http_ereply(conn, 404, "Session Not Found");
			return;
		}
	}
	else if (0 == pl_strcasecmp(&msg->met, "DELETE")) {

		/* draft-ietf-wish-whip-03 */
		info("demo: DELETE -> disconnect\n");

		sess = session_lookup(msg);
		if (sess) {
			info("demo: closing session %s\n", sess->id);
			session_close(sess, 0);

			http_reply(conn, 200, "OK",
				   "Content-Length: 0\r\n"
				   "Access-Control-Allow-Origin: *\r\n"
				   "\r\n");
		}
		else {
			http_ereply(conn, 404, "Session Not Found");
			return;
		}
	}
	else {
		warning("demo: not found: %r %r\n", &msg->met, &msg->path);
		http_ereply(conn, 404, "Not Found");
	}

 out:
	mem_deref(od);
	if (err)
		http_ereply(conn, 500, "Server Error");
}


int demo_init(const char *ice_server,
	      const char *stun_user, const char *credential)
{
	struct pl srv;
	struct sa laddr, laddrs;
	int err;

	if (ice_server) {

		info("demo: using ICE server: %s\n", ice_server);

		pl_set_str(&srv, ice_server);

		err = stunuri_decode(&pc_config.ice_server, &srv);
		if (err) {
			warning("demo: invalid iceserver '%r' (%m)\n",
				&srv, err);
			return err;
		}
	}

	pc_config.stun_user = stun_user;
	pc_config.credential = credential;

	demo.mnat = mnat_find(baresip_mnatl(), "ice");
	if (!demo.mnat) {
		warning("demo: medianat 'ice' not found\n");
		return ENOENT;
	}

	demo.menc = menc_find(baresip_mencl(), "dtls_srtp");
	if (!demo.menc) {
		warning("demo: mediaenc 'dtls-srtp' not found\n");
		return ENOENT;
	}

	sa_set_str(&laddr, "0.0.0.0", HTTP_PORT);
	sa_set_str(&laddrs, "0.0.0.0", HTTPS_PORT);

	err = http_listen(&httpsock, &laddr, http_req_handler, NULL);
	if (err)
		return err;

	err = https_listen(&httpssock, &laddrs, "./share/cert.pem",
			   http_req_handler, NULL);
	if (err)
		return err;

	info("demo: listening on:\n");
	info("    http://%j:%u/\n",
		net_laddr_af(baresip_network(), AF_INET), sa_port(&laddr));
	info("    https://%j:%u/\n",
		net_laddr_af(baresip_network(), AF_INET), sa_port(&laddrs));

	return 0;
}


void demo_close(void)
{
	list_flush(&demo.sessl);

	httpssock = mem_deref(httpssock);
	httpsock = mem_deref(httpsock);
	pc_config.ice_server = mem_deref(pc_config.ice_server);
}
