#ifndef TESTS_STATSRELAYER_STATSRELAYER_H_
#define TESTS_STATSRELAYER_STATSRELAYER_H_

#include <gst/gst.h>
#include <string.h>
#include "common.h"

typedef struct {
  GSList* groups;
}StatsRelayer;

StatsRelayer* make_statsrelayer(void);
void statsrelayer_add_group(StatsRelayer* this, const gchar* string);
void statsrelayer_rem_group(StatsRelayer* this, const gchar* name);
void statsrelayer_flush_group(StatsRelayer* this, const gchar* name);
void statsrelayer_dtor(StatsRelayer* this);

#endif /* TESTS_STATSRELAYER_STATSRELAYER_H_ */
