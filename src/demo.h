/**
 * @file demo.h  Baresip WebRTC demo
 *
 * Copyright (C) 2010 Creytiv.com
 */


/*
 * RTC Session
 *
 * NOTE: API under development
 */

struct stream;
struct rtcsession;

typedef void (rtcsession_gather_h)(void *arg);
typedef void (rtcsession_estab_h)(bool audio, unsigned mediaix, void *arg);
typedef void (rtcsession_close_h)(int err, void *arg);

struct rtcsession_param {
	int unused;
};

int rtcsession_create(struct rtcsession **sessp, const struct config *cfg,
		      const struct rtcsession_param *prm,
		      const struct sa *laddr,
		      struct mbuf *offer,
		      const struct mnat *mnat, const struct menc *menc,
		      struct stun_uri *stun_srv,
		      rtcsession_gather_h *gatherh,
		      rtcsession_estab_h,
		      rtcsession_close_h *closeh, void *arg);
int rtcsession_add_audio(struct rtcsession *sess,
			 const struct config *cfg,
			 struct list *aucodecl);
int rtcsession_add_video(struct rtcsession *sess,
			 const struct config *cfg,
			 struct list *vidcodecl);
int rtcsession_decode_offer(struct rtcsession *sess, struct mbuf *offer);
int rtcsession_encode_answer(struct rtcsession *sess, struct mbuf **mb);
int rtcsession_start_ice(struct rtcsession *sess);
int rtcsession_start_audio(struct rtcsession *sess, unsigned mediaix);
int rtcsession_start_video(struct rtcsession *sess, unsigned mediaix);


int load_file(struct mbuf *mb, const char *filename);


int demo_init(void);
int demo_close(void);
