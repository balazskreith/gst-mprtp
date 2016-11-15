#ifndef TESTS_OWDS_H_
#define TESTS_OWDS_H_

#include <gst/gst.h>
#include <string.h>

typedef struct{
  GstClockTime made;
  GstClockTime delay;
}OWDItem;

typedef void (*OWDLogger)(GstClockTime*);

typedef struct{
  GQueue*      items;
  GstClock*    sysclock;
  GstClockTime length;
  GstClockTime sampling_time;
  GstClockTime last_sampling;
  OWDLogger    logger;
}OWDs;

OWDs* owds_ctor(void);
void owds_dtor(OWDs* this);
OWDs* make_owds(GstClockTime accumulation_length, GstClockTime sampling_time, OWDLogger);

void owds_add_delay(OWDs* this, GstClockTime made, GstClockTime delay);

#endif /* TESTS_OWDS_H_ */
