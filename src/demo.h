/**
 * @file demo.h  Baresip WebRTC demo
 *
 * Copyright (C) 2010 Alfred E. Heggestad
 */


/*
 * NOTE: API under development
 */


struct stream;
struct media_track;
struct session_description;


/*
 * RTCPeerConnection
 *
 * https://developer.mozilla.org/en-US/docs/Web/API/RTCPeerConnection
 */


/* RTCPeerConnection.signalingState */
enum signaling_st {
	SS_STABLE,
	SS_HAVE_LOCAL_OFFER,
	SS_HAVE_REMOTE_OFFER
};


/* RTCConfiguration */
struct configuration {
	struct stun_uri *ice_server;
	const char *stun_user;
	const char *credential;
};

struct peer_connection;

typedef void (peerconnection_gather_h)(void *arg);
typedef void (peerconnection_estab_h)(struct media_track *media,
				      void *arg);
typedef void (peerconnection_close_h)(int err, void *arg);

int  peerconnection_new(struct peer_connection **pcp,
		        const struct configuration *config,
		        const struct mnat *mnat, const struct menc *menc,
		        peerconnection_gather_h *gatherh,
		        peerconnection_estab_h,
		        peerconnection_close_h *closeh, void *arg);
int  peerconnection_add_audio_track(struct peer_connection *pc,
			 const struct config *cfg,
			 struct list *aucodecl);
int  peerconnection_add_video_track(struct peer_connection *pc,
			 const struct config *cfg,
			 struct list *vidcodecl);
int  peerconnection_set_remote_descr(struct peer_connection *pc,
				    const struct session_description *sd);
int  peerconnection_create_offer(struct peer_connection *sess,
				struct mbuf **mb);
int  peerconnection_create_answer(struct peer_connection *sess,
				 struct mbuf **mb);
int  peerconnection_start_ice(struct peer_connection *pc);
void peerconnection_close(struct peer_connection *pc);
void peerconnection_add_ice_candidate(struct peer_connection *pc,
				      const char *cand, const char *mid);
enum signaling_st peerconnection_signaling(const struct peer_connection *pc);


/*
 * HTTP
 */

int http_reply_fmt(struct http_conn *conn, const char *ctype,
		   const char *fmt, ...);
int http_reply_descr(struct http_conn *conn, enum sdp_type type,
		     struct mbuf *mb_sdp);
const char *http_extension_to_mimetype(const char *ext);


/*
 * Demo
 */

int  demo_init(const char *ice_server,
	       const char *stun_user, const char *stun_pass);
void demo_close(void);


/*
 * Media Track
 */

enum media_kind {
	MEDIA_KIND_AUDIO,
	MEDIA_KIND_VIDEO,
};

typedef void (mediatrack_close_h)(int err, void *arg);

/*
 * https://developer.mozilla.org/en-US/docs/Web/API/MediaStreamTrack
 *
 * The MediaStreamTrack interface represents a single media track within
 * a stream; typically, these are audio or video tracks, but other
 * track types may exist as well.
 *
 * NOTE: one-to-one mapping with 'struct stream'
 */
struct media_track {
	struct le le;
	enum media_kind kind;
	union {
		struct audio *au;
		struct video *vid;
		void *p;
	} u;

	bool ice_conn;
	bool dtls_ok;
	bool rtp;
	bool rtcp;

	mediatrack_close_h *closeh;
	void *arg;
};


struct media_track *media_track_add(struct list *lst,
				    enum media_kind kind,
				    mediatrack_close_h *closeh, void *arg);
int  mediatrack_start_audio(struct media_track *media,
			    struct list *ausrcl, struct list *aufiltl);
int  mediatrack_start_video(struct media_track *media);
void mediatrack_stop(struct media_track *media);
enum media_kind mediatrack_kind(const struct media_track *media);
void mediatrack_set_handlers(struct media_track *media);
void mediatrack_summary(const struct media_track *media);
struct stream *media_get_stream(const struct media_track *media);
const char *media_kind_name(enum media_kind kind);
int  mediatrack_debug(struct re_printf *pf, const struct media_track *media);
struct media_track *mediatrack_lookup_media(const struct list *medial,
					    struct stream *strm);
void mediatrack_close(struct media_track *media, int err);
void mediatrack_sdp_attr_decode(struct media_track *media);
