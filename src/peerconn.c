/**
 * @file src/peerconn.c  RTC Peer Connection
 *
 * Copyright (C) 2010 Alfred E. Heggestad
 */

#include <string.h>
#include <re.h>
#include <baresip.h>
#include "demo.h"


struct peer_connection {
	struct stream_param stream_prm;
	struct list streaml;
	struct list medial;
	struct sdp_session *sdp;
	const struct mnat *mnat;
	struct mnat_sess *mnats;
	const struct menc *menc;
	struct menc_sess *mencs;
	struct media_ctx *ctx;
	char cname[16];
	bool got_offer;
	peerconnection_gather_h *gatherh;
	peerconnection_estab_h *estabh;
	peerconnection_close_h *closeh;
	void *arg;

	/* steps: */
	bool gather_ok;
	bool sdp_ok;
};


static void destructor(void *data)
{
	struct peer_connection *pc = data;
	struct le *le;
	size_t i;

	info("peerconnection: destroyed\n");

	info("*** RTCPeerConnection summary ***\n");

	info("steps:\n");
	info(".. gather:   %d\n", pc->gather_ok);
	info(".. sdp:      %d\n", pc->sdp_ok);
	info("\n");

	i=0;
	for (le = pc->medial.head; le; le = le->next) {
		struct media_track *media = le->data;

		if (!media->u.p)
			continue;

		info(".. #%zu '%s'\n", i, media_kind_name(media->kind));
		info(".. ice_conn: %d\n", media->ice_conn);
		info(".. dtls:     %d\n", media->dtls_ok);
		info(".. rtp:      %d\n", media->rtp);
		info(".. rtcp:     %d\n", media->rtcp);
		info("\n");

		++i;
	}

	info("\n");

	for (le = pc->medial.head; le; le = le->next) {
		struct media_track *media = le->data;

		if (!media->u.p)
			continue;

		switch (media->kind) {

		case MEDIA_KIND_AUDIO:
			debug("%H\n", audio_debug, media->u.au);
			break;

		case MEDIA_KIND_VIDEO:
			debug("%H\n", video_debug, media->u.vid);
			break;
		}
	}

	le = pc->medial.head;
	while (le) {
		struct media_track *media = le->data;

		le = le->next;

		mediatrack_stop(media);

		mem_deref(media->u.p);
		mem_deref(media);
	}

	mem_deref(pc->sdp);
	mem_deref(pc->mnats);
	mem_deref(pc->mencs);
}


static struct media_track *lookup_media(struct peer_connection *pc,
					struct stream *strm)
{
	struct le *le;

	for (le = pc->medial.head; le; le = le->next) {
		struct media_track *media = le->data;

		if (strm == media_get_stream(media))
			return media;
	}

	return NULL;
}


static void pc_close(struct peer_connection *pc, int err)
{
	peerconnection_close_h *closeh = pc->closeh;

	pc->closeh = NULL;

	if (closeh)
		closeh(err, pc->arg);
}


static void audio_event_handler(int key, bool end, void *arg)
{
	struct peer_connection *pc = arg;
	(void)pc;
	(void)end;

	info("peerconnection: recv DTMF event: key=%d ('%c')\n", key, key);
}


static void audio_err_handler(int err, const char *str, void *arg)
{
	struct peer_connection *pc = arg;

	warning("peerconnection: audio error: %m (%s)\n", err, str);

	pc_close(pc, err);
}


static void video_err_handler(int err, const char *str, void *arg)
{
	struct peer_connection *pc = arg;

	warning("peerconnection: video error: %m (%s)\n", err, str);

	pc_close(pc, err);
}


static void mnat_estab_handler(int err, uint16_t scode, const char *reason,
			       void *arg)
{
	struct peer_connection *pc = arg;

	if (err) {
		warning("peerconnection: medianat failed: %m\n", err);
		pc_close(pc, err);
		return;
	}
	else if (scode) {
		warning("peerconnection: medianat failed: %u %s\n",
			scode, reason);
		pc_close(pc, EPROTO);
		return;
	}

	info("peerconnection: medianat established/gathered (all streams)\n");

	pc->gather_ok = true;

	if (pc->gatherh)
		pc->gatherh(pc->arg);
}


static void menc_event_handler(enum menc_event event,
			       const char *prm, struct stream *strm,
			       void *arg)
{
	struct peer_connection *pc = arg;
	struct media_track *media;

	media = lookup_media(pc, strm);

	info("peerconnection: mediaenc event '%s' (%s)\n",
	     menc_event_name(event), prm);

	switch (event) {

	case MENC_EVENT_SECURE:
		media->dtls_ok = true;

		stream_set_secure(strm, true);
		stream_start(strm);

		if (pc->estabh)
			pc->estabh(media, pc->arg);
		break;

	default:
		break;
	}
}


static void menc_error_handler(int err, void *arg)
{
	struct peer_connection *pc = arg;

	warning("peerconnection: mediaenc error: %m\n", err);

	if (pc->closeh)
		pc->closeh(err, pc->arg);
}


static void mnatconn_handler(struct stream *strm, void *arg)
{
	struct media_track *media = arg;
	int err;

	info("peerconnection: ice connected (%s)\n", stream_name(strm));

	media->ice_conn = true;

	err = stream_start_mediaenc(strm);
	if (err) {
		pc_close(media->pc, err);
	}
}


static void rtpestab_handler(struct stream *strm, void *arg)
{
	struct media_track *media = arg;

	info("peerconnection: rtp established (%s)\n", stream_name(strm));

	media->rtp = true;
}


static void rtcp_handler(struct stream *strm,
			 struct rtcp_msg *msg, void *arg)
{
	struct media_track *media = arg;
	(void)strm;
	(void)msg;

	media->rtcp = true;
}


static void stream_error_handler(struct stream *strm, int err, void *arg)
{
	struct media_track *media = arg;

	warning("peerconnection: '%s' stream error (%m)\n",
		stream_name(strm), err);

	pc_close(media->pc, err);
}


int peerconnection_create(struct peer_connection **pcp,
			  const struct config *cfg,
			  const struct sa *laddr,
			  struct mbuf *offer,
			  const struct mnat *mnat, const struct menc *menc,
			  struct stun_uri *stun_srv,
			  const char *stun_user, const char *stun_pass,
			  peerconnection_gather_h *gatherh,
			  peerconnection_estab_h *estabh,
			  peerconnection_close_h *closeh, void *arg)
{
	struct peer_connection *pc;
	bool got_offer = offer != NULL;
	int err;

	if (!pcp || !cfg || !laddr)
		return EINVAL;

	if (!mnat || !menc)
		return EINVAL;

	info("peerconnection: create: laddr = %j\n", laddr);

	pc = mem_zalloc(sizeof(*pc), destructor);
	if (!pc)
		return ENOMEM;

	/* RFC 7022 */
	rand_str(pc->cname, sizeof(pc->cname));

	pc->stream_prm.use_rtp = true;
	pc->stream_prm.af      = sa_af(laddr);
	pc->stream_prm.cname   = pc->cname;

	err = sdp_session_alloc(&pc->sdp, laddr);
	if (err)
		goto out;

	if (mnat->sessh) {

		info("peerconnection: using mnat '%s'\n", mnat->id);

		pc->mnat = mnat;

		err = mnat->sessh(&pc->mnats, mnat,
				  net_dnsc(baresip_network()),
				  sa_af(laddr),
				  stun_srv,
				  stun_user, stun_pass,
				  pc->sdp, !got_offer,
				  mnat_estab_handler, pc);
		if (err) {
			warning("peerconnection: medianat session: %m\n", err);
			goto out;
		}
	}

	if (menc->sessh) {
		info("peerconnection: using menc '%s'\n", menc->id);

		pc->menc = menc;

		err = menc->sessh(&pc->mencs, pc->sdp, !got_offer,
				  menc_event_handler,
				  menc_error_handler, pc);
		if (err) {
			warning("peerconnection: mediaenc session: %m\n", err);
			goto out;
		}
	}

	pc->got_offer = got_offer;

	pc->gatherh = gatherh;
	pc->estabh = estabh;
	pc->closeh = closeh;
	pc->arg = arg;

 out:
	if (err)
		mem_deref(pc);
	else
		*pcp = pc;

	return err;
}


int peerconnection_add_audio(struct peer_connection *pc,
			     const struct config *cfg,
			     struct list *aucodecl)
{
	struct media_track *media;
	struct stream *strm;
	int err;

	if (!pc || !cfg || !aucodecl)
		return EINVAL;

	info("peerconnection: add audio (codecs=%u)\n", list_count(aucodecl));

	media = media_track_add(&pc->medial, pc, MEDIA_KIND_AUDIO);

	err = audio_alloc(&media->u.au, &pc->streaml,
			  &pc->stream_prm, cfg,
			  NULL, pc->sdp, 0,
			  pc->mnat, pc->mnats,
			  pc->menc, pc->mencs,
			  20, aucodecl, !pc->got_offer,
			  audio_event_handler, NULL,
			  audio_err_handler, pc);
	if (err) {
		warning("peerconnection: audio alloc failed (%m)\n", err);
		return err;
	}

	audio_set_media_context(media->u.au, &pc->ctx);

	strm = audio_strm(media->u.au);

	stream_set_session_handlers(strm, mnatconn_handler, rtpestab_handler,
				    rtcp_handler, stream_error_handler, media);

	return 0;
}


int peerconnection_add_video(struct peer_connection *pc,
			 const struct config *cfg,
			 struct list *vidcodecl)
{
	struct media_track *media;
	struct stream *strm;
	int err;

	if (!pc || !cfg || !vidcodecl)
		return EINVAL;

	info("peerconnection: add video (codecs=%u)\n", list_count(vidcodecl));

	media = media_track_add(&pc->medial, pc, MEDIA_KIND_VIDEO);

	err = video_alloc(&media->u.vid, &pc->streaml,
			  &pc->stream_prm,
			  cfg,
			  pc->sdp, 0,
			  pc->mnat, pc->mnats,
			  pc->menc, pc->mencs,
			  NULL, vidcodecl,
			  NULL,
			  !pc->got_offer,
			  video_err_handler, pc);
	if (err) {
		warning("peerconnection: video alloc failed (%m)\n", err);
		return err;
	}

	strm = video_strm(media->u.vid);

	stream_set_session_handlers(strm, mnatconn_handler,
				    rtpestab_handler,
				    rtcp_handler,
				    stream_error_handler, media);

	return 0;
}


int peerconnection_decode_descr(struct peer_connection *pc, struct mbuf *sdp,
				bool offer)
{
	struct le *le;
	int err;

	if (!pc || !sdp)
		return EINVAL;

	info("peerconnection: decode %s\n", offer ? "offer" : "answer");

	if (LEVEL_DEBUG == log_level_get()) {
		info("- - %s - -\n", offer ? "offer" : "answer");
		info("%b\n", (sdp)->buf, (sdp)->end);
		info("- - - - - - -\n");
	}

	err = sdp_decode(pc->sdp, sdp, offer);
	if (err) {
		warning("peerconnection: sdp decode failed (%m)\n", err);
		return err;
	}

	/* must be done after sdp_decode() */
	for (le = pc->medial.head; le; le = le->next) {
		struct media_track *media = le->data;

		if (!media->u.p)
			continue;

		switch (media->kind) {

		case MEDIA_KIND_VIDEO:
			video_sdp_attr_decode(media->u.vid);
			break;

		default:
			break;
		}
	}

	/* must be done after sdp_decode() */
	for (le = pc->streaml.head; le; le = le->next) {
		struct stream *strm = le->data;

		stream_update(strm);
	}

	return 0;
}


int peerconnection_create_offer(struct peer_connection *pc, struct mbuf **mb)
{
	int err;

	if (!pc)
		return EINVAL;

	if (!pc->gather_ok) {
		warning("peerconnection: sdp: ice not gathered\n");
		return EPROTO;
	}

	info("peerconnection: create offer\n");

	err = sdp_encode(mb, pc->sdp, true);
	if (err)
		return err;

	if (LEVEL_DEBUG == log_level_get()) {
		info("- - offer - -\n");
		info("%b\n", (*mb)->buf, (*mb)->end);
		info("- - - - - - -\n");
	}

	pc->sdp_ok = true;

	return 0;
}


int peerconnection_create_answer(struct peer_connection *pc,
				 struct mbuf **mb)
{
	int err;

	if (!pc)
		return EINVAL;

	if (!pc->gather_ok) {
		warning("peerconnection: sdp: ice not gathered\n");
		return EPROTO;
	}

	info("peerconnection: create answer\n");

	err = sdp_encode(mb, pc->sdp, false);
	if (err)
		return err;

	if (LEVEL_DEBUG == log_level_get()) {
		info("- - answer - -\n");
		info("%b\n", (*mb)->buf, (*mb)->end);
		info("- - - - - - -\n");
	}

	pc->sdp_ok = true;

	return 0;
}


int peerconnection_start_ice(struct peer_connection *pc)
{
	int err;

	if (!pc)
		return EINVAL;

	info("peerconnection: start ice\n");

	if (!pc->sdp_ok) {
		warning("peerconnection: ice: sdp not ready\n");
		return EPROTO;
	}

	if (pc->mnat->updateh && pc->mnats) {
		err = pc->mnat->updateh(pc->mnats);
		if (err) {
			warning("peerconnection: mnat update failed (%m)\n",
				err);
			return err;
		}
	}

	return 0;
}


/* todo: replace with signalingstate */
bool peerconnection_got_offer(const struct peer_connection *pc)
{
	return pc ? pc->got_offer : false;
}


void peerconnection_close(struct peer_connection *pc)
{
	if (!pc)
		return;

	pc->closeh = NULL;
	pc->mnats = mem_deref(pc->mnats);
}
