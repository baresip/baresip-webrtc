/**
 * @file main.c Main application code
 *
 * Copyright (C) 2010 Creytiv.com
 */

#include <string.h>
#include <stdlib.h>
#include <getopt.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "demo.h"


#define DEBUG_MODULE "baresip-webrtc"
#define DEBUG_LEVEL 6
#include <re_dbg.h>


static const char *modv[] = {
	"ice",
	"dtls_srtp",
	"g711",
	"aufile",
	"cairo",
	"vp8"
};


static void signal_handler(int signum)
{
	(void)signum;

	re_fprintf(stderr, "terminated on signal %d\n", signum);

	re_cancel();
}


static void usage(void)
{
	re_fprintf(stderr,
		   "Usage: baresip-webrtc [options]\n"
		   "\n"
		   "options:\n"
		   "\t-f <path>        Config path\n"
                   "\t-h               Help\n"
		   "\n");
}


int main(int argc, char *argv[])
{
	struct config *config;
	size_t i;
	int err = 0;

	for (;;) {

		const int c = getopt(argc, argv, "hl:f:u:t");
		if (0 > c)
			break;

		switch (c) {

		case '?':
		default:
			err = EINVAL;
			/*@fallthrough@*/
		case 'f':
			conf_path_set(optarg);
			break;
		case 'h':
			usage();
			return err;
		}
	}

	if (argc < 1 || (argc != (optind + 0))) {
		usage();
		return -2;
	}

	err = libre_init();
	if (err) {
		(void)re_fprintf(stderr, "libre_init: %m\n", err);
		goto out;
	}

	(void)sys_coredump_set(true);

	err = conf_configure();
	if (err) {
		warning("main: configure failed: %m\n", err);
		goto out;
	}

	/*
	 * Initialise the top-level baresip struct, must be
	 * done AFTER configuration is complete.
	 */
	err = baresip_init(conf_config());
	if (err) {
		warning("main: baresip init failed (%m)\n", err);
		goto out;
	}

	for (i=0; i<ARRAY_SIZE(modv); i++) {

		err = module_load(modv[i]);
		if (err) {
			re_fprintf(stderr,
				   "could not pre-load module"
				   " '%s' (%m)\n", modv[i], err);
		}
	}

	config = conf_config();

	str_ncpy(config->audio.src_mod, "aufile",
		 sizeof(config->audio.src_mod));
	str_ncpy(config->audio.src_dev, "./share/sine_8000.wav",
		 sizeof(config->audio.src_dev));

	err = demo_init();
	if (err) {
		re_fprintf(stderr, "failed to init demo: %m\n", err);
		goto out;
	}

	(void)re_main(signal_handler);

	re_printf("Bye for now\n");

 out:
	demo_close();

	/* note: must be done before mod_close() */
	module_app_unload();

	conf_close();

	baresip_close();

	/* NOTE: modules must be unloaded after all application
	 *       activity has stopped.
	 */
	debug("main: unloading modules..\n");
	mod_close();

	libre_close();

	/* Check for memory leaks */
	tmr_debug();
	mem_debug();

	return err;
}
