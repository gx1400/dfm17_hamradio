/*
 * fake_settings.h: settings for the tests that link settings.c. provision()
 * gives the live settings (cfg) the defaults with the callsign NZ0P, i.e. a
 * board that has been set up and may transmit.
 */
#ifndef FAKE_SETTINGS_H_
#define FAKE_SETTINGS_H_

#include "settings.h"

static inline void provision_as(settings_t *s, const char *call) {
	settings_defaults(s);
	for (int i = 0; i < 6; i++)
		s->callsign[i] = 0;
	for (int i = 0; i < 6 && call[i]; i++)
		s->callsign[i] = call[i];
	settings_seal(s);
}

static inline void provision(void) {
	provision_as(&cfg, "NZ0P");
}

#endif /* FAKE_SETTINGS_H_ */
