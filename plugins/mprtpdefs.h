/*
 * mprtpdefs.h
 *
 *  Created on: Mar 14, 2016
 *      Author: balazs
 */

#ifndef PLUGINS_MPRTPDEFS_H_
#define PLUGINS_MPRTPDEFS_H_


#define MPRTP_DEFAULT_EXTENSION_HEADER_ID 3
#define ABS_TIME_DEFAULT_EXTENSION_HEADER_ID 8
#define FEC_PAYLOAD_DEFAULT_ID 126
#define SUBFLOW_DEFAULT_SENDING_RATE 100000

#define MPRTP_PLUGIN_MAX_SUBFLOW_NUM 32

#define DISABLE_LINE if(0)


#define PROFILING(msg, func) \
{  \
  GstClockTime start, elapsed; \
  start = _now(this); \
  func; \
  elapsed = GST_TIME_AS_MSECONDS(_now(this) - start); \
  if(0 < elapsed) {g_print(msg" elapsed time in ms: %lu\n", elapsed); }\
} \

#endif /* PLUGINS_MPRTPDEFS_H_ */
