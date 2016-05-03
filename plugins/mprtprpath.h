/*
 * mprtpssubflow.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef MPRTPRSUBFLOW_H_
#define MPRTPRSUBFLOW_H_

#include <gst/gst.h>
#include <gst/rtp/gstrtpbuffer.h>
#include <gst/rtp/gstrtcpbuffer.h>
#include <gst/base/gstqueuearray.h>
#include "gstmprtcpbuffer.h"
#include "bintree.h"
#include "numstracker.h"
#include "percentiletracker2.h"
#include "gstmprtpbuffer.h"
#include "percentiletracker.h"
#include "packetsrcvtracker.h"

G_BEGIN_DECLS

typedef struct _MpRTPReceiverPath MpRTPRPath;
typedef struct _MpRTPReceiverPathClass MpRTPRPathClass;


#define MPRTPR_PATH_TYPE             (mprtpr_path_get_type())
#define MPRTPR_PATH(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),MPRTPR_PATH_TYPE,MPRTPRPath))
#define MPRTPR_PATH_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),MPRTPR_PATH_TYPE,MPRTPRPathClass))
#define MPRTPR_PATH_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),MPRTPR_PATH_TYPE))
#define MPRTPR_PATH_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),MPRTPR_PATH_TYPE))
#define MPRTPR_PATH_CAST(src)        ((MpRTPRPath *)(src))


struct _MpRTPReceiverPath
{
  GObject                   object;
  guint8                    id;
  GRWLock                   rwmutex;
  GstClock*                 sysclock;

  gboolean                  seq_initialized;
  guint16                   cycle_num;

  gint32                    jitter;
  guint16                   highest_seq;
  guint32                   total_packets_received;

  guint32                   last_rtp_timestamp;

  gdouble                   path_skew;
  gdouble                   path_avg_delay;
  GstClockTime              last_mprtp_delay;
  PercentileTracker*        delays;
  PercentileTracker2*       skews;

  PacketsRcvTracker*        packetstracker;

  gboolean                  urgent;

  gboolean                  spike_mode;
  GstClockTime              spike_var;
  GstClockTime              spike_var_treshold;
  GstClockTime              spike_delay_treshold;
  GstClockTime              last_added_delay;
  GstClockTime              last_last_added_delay;
};

struct _MpRTPReceiverPathClass
{
  GObjectClass parent_class;
};


GType mprtpr_path_get_type (void);
void mprtpr_path_destroy(gpointer ptr);
MpRTPRPath *make_mprtpr_path (guint8 id);
//guint64 mprtpr_path_get_packet_skew_median (MPRTPRPath * this);

guint8 mprtpr_path_get_id (MpRTPRPath * this);
guint16 mprtpr_path_get_HSSN (MpRTPRPath * this);
void mprtpr_path_get_regular_stats(MpRTPRPath *this,
                              guint16 *HSN,
                              guint16 *cycle_num,
                              guint32 *jitter,
                              guint32 *received_num);


void mprtpr_path_get_total_receivements (MpRTPRPath * this,
                               guint32 *total_packets_received,
                               guint32 *total_payload_received);

void mprtpr_path_get_owd_stats(MpRTPRPath *this,
                                 GstClockTime *median,
                                 GstClockTime *min,
                                 GstClockTime* max);
gboolean
mprtpr_path_is_in_spike_mode(MpRTPRPath *this);

void
mprtpr_path_set_spike_treshold(MpRTPRPath *this, GstClockTime delay_treshold, GstClockTime var_treshold);

gboolean
mprtpr_path_is_urgent_request(MpRTPRPath *this);

void
mprtpr_path_set_urgent_request(MpRTPRPath *this);

void
mprtpr_path_set_discard_treshold(MpRTPRPath *this, GstClockTime treshold);

void
mprtpr_path_set_lost_treshold(MpRTPRPath *this, GstClockTime treshold);

PacketsRcvTracker*
mprtpr_path_ref_packetstracker(MpRTPRPath *this);

PacketsRcvTracker*
mprtpr_path_unref_packetstracker(MpRTPRPath *this);

void
mprtpr_path_set_owd_window_treshold(MpRTPRPath *this, GstClockTime treshold);

void
mprtpr_path_set_spike_delay_treshold(MpRTPRPath *this, GstClockTime delay_treshold);

void
mprtpr_path_set_spike_var_treshold(MpRTPRPath *this, GstClockTime var_treshold);

void mprtpr_path_get_joiner_stats(MpRTPRPath *this,
                           gdouble       *path_delay,
                           gdouble       *path_skew);

void mprtpr_path_add_delay(MpRTPRPath *this, GstClockTime delay);

void mprtpr_path_process_rtp_packet(MpRTPRPath *this,
                                    GstMpRTPBuffer *mprtp);

G_END_DECLS
#endif /* MPRTPRSUBFLOW_H_ */
