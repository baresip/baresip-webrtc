/**
 * @file src/session.c  RTC Session
 *
 * Copyright (C) 2010 Creytiv.com
 */

#include <string.h>
#include <re.h>
#include <baresip.h>
#include "demo.h"


struct rtcsession {
	struct list streaml;
	struct sdp_session *sdp;
	struct audio *au;
	const struct mnat *mnat;
	struct mnat_sess *mnats;
	struct menc_sess *mencs;
	char cname[16];
	rtcsession_gather_h *gatherh;
	rtcsession_estab_h *estabh;
	rtcsession_close_h *closeh;
	void *arg;

	/* steps: */
	bool gather_ok;
	bool sdp_ok;
	bool ice_conn;
	bool dtls_ok;
};


static void destructor(void *data)
{
	struct rtcsession *sess = data;

	info("rtcsession: destroyed\n");

	info("steps:\n");
	info(".. gather:   %d\n", sess->gather_ok);
	info(".. sdp:      %d\n", sess->sdp_ok);
	info(".. ice_conn: %d\n", sess->ice_conn);
	info(".. dtls:     %d\n", sess->dtls_ok);
	info("\n");

	info("*** Session *** %H\n", audio_debug, sess->au);

	mem_deref(sess->au);
	mem_deref(sess->sdp);
	mem_deref(sess->mnats);
	mem_deref(sess->mencs);
}


static void session_close(struct rtcsession *sess, int err)
{
	rtcsession_close_h *closeh = sess->closeh;

	sess->closeh = NULL;

	if (closeh)
		closeh(err, sess->arg);
}


static void audio_event_handler(int key, bool end, void *arg)
{
	struct rtcsession *sess = arg;
	(void)sess;

	info("rtcsession: recv event: key=%d ('%c')\n", key, key);
}


static void audio_err_handler(int err, const char *str, void *arg)
{
	struct rtcsession *sess = arg;

	warning("rtcsession: audio error: %m (%s)\n", err, str);

	session_close(sess, err);
}


static void mnat_estab_handler(int err, uint16_t scode, const char *reason,
			       void *arg)
{
	struct rtcsession *sess = arg;

	if (err) {
		warning("rtcsession: medianat failed: %m\n", err);
		session_close(sess, err);
		return;
	}
	else if (scode) {
		warning("rtcsession: medianat failed: %u %s\n", scode, reason);
		session_close(sess, EPROTO);
		return;
	}

	info("rtcsession: medianat established/gathered\n");

	sess->gather_ok = true;

	if (sess->gatherh)
		sess->gatherh(sess->arg);
}


static void menc_event_handler(enum menc_event event,
			       const char *prm, void *arg)
{
	struct rtcsession *sess = arg;
	struct stream *strm;

	info("rtcsession: mediaenc event '%s' (%s)\n",
	     menc_event_name(event), prm);

	switch (event) {

	case MENC_EVENT_SECURE:
		sess->dtls_ok = true;  /* todo: all streams */

		if (strstr(prm, "audio")) {
			stream_set_secure(audio_strm(sess->au), true);
			stream_start(audio_strm(sess->au));

			strm = audio_strm(sess->au);

			if (sess->estabh)
				sess->estabh(strm, sess->arg);
		}
		else {
			info("rtcsession: mediaenc: no match for stream"
			     " (%s)\n", prm);
		}
		break;

	default:
		break;
	}
}


static void menc_error_handler(int err, void *arg)
{
	struct rtcsession *sess = arg;

	warning("rtcsession: mediaenc error: %m\n", err);

	if (sess->closeh)
		sess->closeh(err, sess->arg);
}


static void mnatconn_handler(struct stream *strm, void *arg)
{
	struct rtcsession *sess = arg;
	int err;

	info("rtcsession: ice connected.\n");

	/* todo: check all streams */
	sess->ice_conn = true;

	err = stream_start_mediaenc(strm);
	if (err) {
		session_close(sess, err);
	}
}


int rtcsession_create(struct rtcsession **sessp, const struct config *cfg,
		      const struct rtcsession_param *prm,
		      struct list *aucodecl, const struct sa *laddr,
		      struct mbuf *offer,
		      const struct mnat *mnat, const struct menc *menc,
		      const char *stun_server, uint16_t stun_port,
		      rtcsession_gather_h *gatherh,
		      rtcsession_estab_h *estabh,
		      rtcsession_close_h *closeh, void *arg)
{
	struct stream_param stream_prm = {
		.use_rtp = true,
		.af      = sa_af(laddr),
	};
	struct rtcsession *sess;
	struct le *le;
	bool got_offer = offer != NULL;
	int err;

	if (!sessp || !cfg || !prm || !laddr)
		return EINVAL;

	if (!mnat || !menc)
		return EINVAL;

	info("rtcsession: create session, laddr = %j\n", laddr);

	sess = mem_zalloc(sizeof(*sess), destructor);
	if (!sess)
		return ENOMEM;

	rand_str(sess->cname, sizeof(sess->cname));
	stream_prm.cname = sess->cname;

	err = sdp_session_alloc(&sess->sdp, laddr);
	if (err)
		goto out;

	if (mnat->sessh) {

		info("rtcsession: using mnat '%s'\n", mnat->id);

		sess->mnat = mnat;

		err = mnat->sessh(&sess->mnats, mnat,
				  net_dnsc(baresip_network()),
				  sa_af(laddr),
				  stun_server, stun_port,
				  NULL, NULL,
				  sess->sdp, !got_offer,
				  mnat_estab_handler, sess);
		if (err) {
			warning("rtcsession: medianat session: %m\n", err);
			goto out;
		}
	}

	if (menc->sessh) {
		info("rtcsession: using menc '%s'\n", menc->id);

		err = menc->sessh(&sess->mencs, sess->sdp, !got_offer,
				  menc_event_handler,
				  menc_error_handler, sess);
		if (err) {
			warning("rtcsession: mediaenc session: %m\n", err);
			goto out;
		}
	}

	if (prm->audio) {
		err = audio_alloc(&sess->au, &sess->streaml, &stream_prm, cfg,
				  NULL, sess->sdp, 0,
				  mnat, sess->mnats,
				  menc, sess->mencs,
				  20, aucodecl, !got_offer,
				  audio_event_handler,
				  audio_err_handler, sess);
		if (err) {
			warning("rtcsession: audio alloc failed (%m)\n", err);
			goto out;
		}
	}

	if (prm->video) {
		warning("rtcsession: no video yet ..\n");
		err = ENOTSUP;
		goto out;
	}

	for (le = sess->streaml.head; le; le = le->next) {
		struct stream *strm = le->data;

		stream_set_session_handlers(strm, mnatconn_handler,
					    NULL, sess);
	}

	if (offer) {
		err = sdp_decode(sess->sdp, offer, true);
		if (err) {
			warning("rtcsession: sdp decode failed (%m)\n", err);
			goto out;
		}
	}

	/* must be done after sdp_decode() */
	stream_update(audio_strm(sess->au));

	sess->gatherh = gatherh;
	sess->estabh = estabh;
	sess->closeh = closeh;
	sess->arg = arg;

 out:
	if (err)
		mem_deref(sess);
	else
		*sessp = sess;

	return err;
}


int rtcsession_encode_answer(struct rtcsession *sess, struct mbuf **mb)
{
	int err;

	if (!sess)
		return EINVAL;

	if (!sess->gather_ok) {
		warning("rtcsession: sdp: ice not gathered\n");
		return EPROTO;
	}

	info("rtcsession: encode answer\n");

	err = sdp_encode(mb, sess->sdp, false);
	if (err)
		return err;

	sess->sdp_ok = true;

	return 0;
}


int rtcsession_start_ice(struct rtcsession *sess)
{
	int err;

	if (!sess)
		return EINVAL;

	if (!sess->sdp_ok) {
		warning("rtcsession: ice: sdp not ready\n");
		return EPROTO;
	}

	if (sess->mnat->updateh && sess->mnats) {
		err = sess->mnat->updateh(sess->mnats);
		if (err) {
			warning("rtcsession: mnat update failed (%m)\n", err);
			return err;
		}
	}

	return 0;
}


int rtcsession_start_audio(struct rtcsession *sess)
{
	const struct sdp_format *sc;
	int err = 0;

	if (!sess)
		return EINVAL;

	if (!sess->ice_conn || !sess->dtls_ok) {
		warning("rtcsession: start: ice or dtls not ready\n");
		return EPROTO;
	}

	info("rtcsession: start audio\n");

	/* Audio Stream */
	sc = sdp_media_rformat(stream_sdpmedia(audio_strm(sess->au)), NULL);
	if (sc) {
		struct aucodec *ac = sc->data;

		err  = audio_encoder_set(sess->au, ac, sc->pt, sc->params);
		if (err) {
			warning("rtcsession: start:"
				" audio_encoder_set error: %m\n", err);
			return err;
		}

		err = audio_start(sess->au);
		if (err) {
			warning("rtcsession: start:"
				" audio_start error: %m\n", err);
			return err;
		}
	}
	else {
		info("rtcsession: audio stream is disabled..\n");
	}

	return 0;
}
