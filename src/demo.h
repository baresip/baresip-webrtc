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


/*
 * RTCPeerConnection
 *
 * https://developer.mozilla.org/en-US/docs/Web/API/RTCPeerConnection
 */

struct peer_connection;

typedef void (peerconnection_gather_h)(void *arg);
typedef void (peerconnection_estab_h)(struct media_track *media,
				      void *arg);
typedef void (peerconnection_close_h)(int err, void *arg);

int peerconnection_create(struct peer_connection **pcp,
			  const struct config *cfg,
			  const struct sa *laddr,
			  struct mbuf *offer,
			  const struct mnat *mnat, const struct menc *menc,
			  struct stun_uri *stun_srv,
			  const char *stun_user, const char *stun_pass,
			  peerconnection_gather_h *gatherh,
			  peerconnection_estab_h,
			  peerconnection_close_h *closeh, void *arg);
int peerconnection_add_audio(struct peer_connection *pc,
			 const struct config *cfg,
			 struct list *aucodecl);
int peerconnection_add_video(struct peer_connection *pc,
			 const struct config *cfg,
			 struct list *vidcodecl);
int peerconnection_decode_descr(struct peer_connection *pc, struct mbuf *sdp,
			    bool offer);
int peerconnection_create_offer(struct peer_connection *sess,
				struct mbuf **mb);
int peerconnection_create_answer(struct peer_connection *sess,
				 struct mbuf **mb);
int peerconnection_start_ice(struct peer_connection *pc);
bool peerconnection_got_offer(const struct peer_connection *pc);
void peerconnection_close(struct peer_connection *pc);


/*
 * Util
 */

int load_file(struct mbuf *mb, const char *filename);
const char *file_extension(const char *filename);


/*
 * Demo
 */

int demo_init(const char *ice_server,
	      const char *stun_user, const char *stun_pass);
int demo_close(void);


/*
 * Session Description
 */

/*
 * https://developer.mozilla.org/en-US/docs/Web/API/RTCSessionDescription
 *
 * format:
 *
 * {
 *   "type" : "answer",
 *   "sdp" : "v=0\r\ns=-\r\n..."
 * }
 */
struct session_description {
	char type[32];     /* offer, answer, ... */
	struct mbuf *sdp;
};

int session_description_encode(struct odict **odp,
			       const char *type, struct mbuf *sdp);
int session_description_decode(struct session_description *sd,
			       struct mbuf *mb);
void session_description_reset(struct session_description *sd);


/*
 * Media Track
 */

enum media_kind {
	MEDIA_KIND_AUDIO,
	MEDIA_KIND_VIDEO,
};

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

	struct peer_connection *pc;  /* pointer to parent */
	bool ice_conn;
	bool dtls_ok;
	bool rtp;
	bool rtcp;
};

struct media_track *media_track_add(struct list *lst,
				    struct peer_connection *pc,
				    enum media_kind kind);
int  mediatrack_start_audio(struct media_track *media,
			    struct list *ausrcl, struct list *aufiltl);
int  mediatrack_start_video(struct media_track *media);
void mediatrack_stop(struct media_track *media);
struct stream *media_get_stream(const struct media_track *media);
const char *media_kind_name(enum media_kind kind);
