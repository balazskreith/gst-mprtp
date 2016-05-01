/*
 * packetsrcvtracker.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef PACKETSRCVTRACKER_H_
#define PACKETSRCVTRACKER_H_

#include <gst/gst.h>
#include "gstmprtcpbuffer.h"
#include "gstmprtpbuffer.h"
#include "percentiletracker2.h"

typedef struct _PacketsRcvTracker PacketsRcvTracker;
typedef struct _PacketsRcvTrackerClass PacketsRcvTrackerClass;

#define PACKETSRCVTRACKER_TYPE             (packetsrcvtracker_get_type())
#define PACKETSRCVTRACKER(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),PACKETSRCVTRACKER_TYPE,PacketsRcvTracker))
#define PACKETSRCVTRACKER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),PACKETSRCVTRACKER_TYPE,PacketsRcvTrackerClass))
#define PACKETSRCVTRACKER_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),PACKETSRCVTRACKER_TYPE))
#define PACKETSRCVTRACKER_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),PACKETSRCVTRACKER_TYPE))
#define PACKETSRCVTRACKER_CAST(src)        ((PacketsRcvTracker *)(src))


typedef struct _PacketsRcvTrackerItem{
  guint16      seq;
  GstClockTime added;
  gboolean     received;
  gboolean     missed;
  gboolean     discarded;
  gboolean     lost;
  guint        ref;
  guint        payload_len;
}PacketsRcvTrackerItem;

typedef struct _PacketsRcvTrackerStat
{
  guint32                  total_packets_discarded_or_lost;
  guint32                  total_packets_lost;
  guint32                  total_packets_discarded;
  guint32                  total_payload_discarded;
  guint32                  total_packets_received;
  guint32                  total_payload_received;
  guint32                  good_packets_in_1s;
  guint32                  good_payload_bytes_in_1s;
  guint16                  highest_seq;
  guint16                  cycle_num;
}PacketsRcvTrackerStat;

typedef struct _CorrBlock CorrBlock;

struct _PacketsRcvTracker
{
  GObject                  object;
  GRWLock                  rwmutex;
  GstClock*                sysclock;

  GQueue*                  missed;
  GQueue*                  discarded;
  GQueue*                  packets;

  gint32                   reported_seq;
  GstClockTime             discard_treshold;
  GstClockTime             lost_treshold;

  PacketsRcvTrackerStat    trackerstat;


  guint16                  cycle_num;
  gboolean                 initialized;

  struct{
    guint64 last_ntp_snd_time;
    guint64 last_ntp_rcv_time;
    GstClockTime last_delay;
    guint32 last_timestamp;
  }devar;

  guint32 cblocks_counter;
  CorrBlock *blocks;
  gdouble aaaa;
  guint block_index;
  guint remb_state;
  GstClockTime remb_ts;

  gdouble                 path_skew;

};

struct _PacketsRcvTrackerClass{
  GObjectClass parent_class;

};

GType packetsrcvtracker_get_type (void);
PacketsRcvTracker *make_packetsrcvtracker(void);
void packetsrcvtracker_reset(PacketsRcvTracker *this);
void packetsrcvtracker_set_lost_treshold(PacketsRcvTracker *this, GstClockTime treshold);
void packetsrcvtracker_set_discarded_treshold(PacketsRcvTracker *this, GstClockTime treshold);
void packetsrcvtracker_add(PacketsRcvTracker *this, GstMpRTPBuffer *mprtp);
void packetsrcvtracker_update_reported_sn(PacketsRcvTracker *this, guint16 reported_sn);
gdouble packetsrcvtracker_get_remb(PacketsRcvTracker * this, guint16 *hssn);
void packetsrcvtracker_set_bitvectors(PacketsRcvTracker * this,
                                     guint16 *begin_seq,
                                     guint16 *end_seq,
                                     GstRTCPXRChunk *chunks,
                                     guint *chunks_length);
void packetsrcvtracker_get_stat (PacketsRcvTracker * this, PacketsRcvTrackerStat* result);

#endif /* PACKETSRCVTRACKER_H_ */
