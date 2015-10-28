/*
 * smanctrler.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef STREAM_JOINER_H_
#define STREAM_JOINER_H_

#include <gst/gst.h>
#include "playoutwindow.h"

typedef struct _StreamJoiner StreamJoiner;
typedef struct _StreamJoinerClass StreamJoinerClass;
typedef struct _Heap Heap;

#include "mprtprpath.h"

#define STREAM_JOINER_TYPE             (stream_joiner_get_type())
#define STREAM_JOINER(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),STREAM_JOINER_TYPE,StreamJoiner))
#define STREAM_JOINER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),STREAM_JOINER_TYPE,StreamJoinerClass))
#define STREAM_JOINER_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),STREAM_JOINER_TYPE))
#define STREAM_JOINER_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),STREAM_JOINER_TYPE))
#define STREAM_JOINER_CAST(src)        ((StreamJoiner *)(src))

#define MPRTP_SENDER_SCHTREE_MAX_PATH_NUM 32
#define MAX_SKEWS_ARRAY_LENGTH 256



struct _StreamJoiner
{
  GObject               object;

  GstTask*             thread;
  GRecMutex            thread_mutex;
  GHashTable*          subflows;
  GRWLock              rwmutex;

//  GstClockTime      max_path_skew;
  BinTree*             max_skews_tree;
  guint64              max_skews[MAX_SKEWS_ARRAY_LENGTH];
  guint8               max_skews_index;
  Heap*                packets_heap;
  guint16              popped_hsn;
  GQueue*              heap_items_pool;
  GQueue*              packets_framequeue;
  PlayoutWindow*       playoutwindow;
  gboolean             obsolate_automatically;
  gboolean             playout_allowed;
  gboolean             playout_halt;
  GstClockTime         playout_halt_time;
  gint                 subflow_num;
  GstClock*            sysclock;
  void               (*send_mprtp_packet_func)(gpointer,GstBuffer*);
  gpointer             send_mprtp_packet_data;

  GstClockTime         last_obsolate_checked;
};

struct _StreamJoinerClass{
  GObjectClass parent_class;
};

//Class functions
void stream_joiner_set_sending(StreamJoiner* this, gpointer data, void (*func)(gpointer,GstBuffer*));

void
stream_joiner_add_path(StreamJoiner * this, guint8 subflow_id, MpRTPRPath *path);

void
stream_joiner_rem_path(StreamJoiner * this, guint8 subflow_id);


void
stream_joiner_path_obsolation(StreamJoiner *this, gboolean obsolate_automatically);

void
stream_joiner_receive_rtp(StreamJoiner * this, GstRTPBuffer *rtp);

void
stream_joiner_set_playout_halt_time(StreamJoiner *this, GstClockTime halt_time);
void
stream_joiner_set_playout_allowed(StreamJoiner *this, gboolean playout_permission);

GType
stream_joiner_get_type (void);

#endif /* STREAM_JOINER_H_ */
