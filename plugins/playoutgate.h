/*
 * playoutgate.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef PLAYOUTGATE_H_
#define PLAYOUTGATE_H_

#include <gst/gst.h>

typedef struct _PlayoutGate PlayoutGate;
typedef struct _PlayoutGateClass PlayoutGateClass;

#define PLAYOUTGATE_TYPE             (playoutgate_get_type())
#define PLAYOUTGATE(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),PLAYOUTGATE_TYPE,PlayoutGate))
#define PLAYOUTGATE_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),PLAYOUTGATE_TYPE,PlayoutGateClass))
#define PLAYOUTGATE_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),PLAYOUTGATE_TYPE))
#define PLAYOUTGATE_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),PLAYOUTGATE_TYPE))
#define PLAYOUTGATE_CAST(src)        ((PlayoutGate *)(src))

typedef struct _FrameNode FrameNode;
typedef struct _Frame Frame;

struct _PlayoutGate
{
  GObject                  object;
  Frame*                   head;
  Frame*                   tail;
  GstClockTime             max_delay;
  GRWLock                  rwmutex;
  GQueue*                  framenodes_pool;
  GQueue*                  frames_pool;
  GstClock*                sysclock;
};

struct _PlayoutGateClass{
  GObjectClass parent_class;

};
void playoutgate_test(void);
GType playoutgate_get_type (void);
PlayoutGate *make_playoutgate(void);
gboolean playoutgate_is_diversified(PlayoutGate *this);
void playoutgate_reset(PlayoutGate *this);
void playoutgate_push(PlayoutGate *this, GstRTPBuffer *rtp, guint8 subflow_id);
GstBuffer *playoutgate_pop(PlayoutGate *this);
gboolean playoutgate_has_frame_to_playout(PlayoutGate *this);
void playoutgate_set_max_delay(PlayoutGate *this, GstClockTime window_size);
#endif /* PLAYOUTGATE_H_ */
