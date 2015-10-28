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
#include "packetsqueue.h"

G_BEGIN_DECLS

typedef struct _MpRTPReceiverPath MpRTPRPath;
typedef struct _MpRTPReceiverPathClass MpRTPRPathClass;
typedef struct _MpRTPRReceivedItem  MpRTPRReceivedItem;

#include "streamjoiner.h"

#define MPRTPR_PACKET_INIT           {NULL, 0, 0, 0}

#define MPRTPR_PATH_TYPE             (mprtpr_path_get_type())
#define MPRTPR_PATH(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),MPRTPR_PATH_TYPE,MPRTPRPath))
#define MPRTPR_PATH_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),MPRTPR_PATH_TYPE,MPRTPRPathClass))
#define MPRTPR_PATH_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),MPRTPR_PATH_TYPE))
#define MPRTPR_PATH_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),MPRTPR_PATH_TYPE))
#define MPRTPR_PATH_CAST(src)        ((MpRTPRPath *)(src))

#define SKEWS_ARRAY_LENGTH 256

struct _MpRTPReceiverPath
{
  GObject       object;
  guint8        id;
  GRWLock       rwmutex;

  GList*        gaps;
  GList*        result;
  //guint8        ext_header_id;
  gboolean      seq_initialized;
  //gboolean      skew_initialized;
  guint16       cycle_num;
  guint32       jitter;

  guint32       total_late_discarded;
  guint32       total_late_discarded_bytes;
//  guint32       total_bytes_received;
  guint32       total_payload_bytes;
  guint32       total_early_discarded;
  guint32       total_duplicated_packet_num;
  guint16       highest_seq;

  guint64       ext_rtptime;
  guint64       last_packet_skew;
  GstClockTime  last_received_time;
  GstClock*     sysclock;
  guint16       played_highest_seq;
  guint32       total_packet_losts;
  guint64       total_packets_received;
  PacketsQueue *packetsqueue;
  guint32       last_rtp_timestamp;
  BinTree*      min_skew_bintree;
  BinTree*      max_skew_bintree;
  guint32       skew_bytes;
  guint8        skews_payload_octets[SKEWS_ARRAY_LENGTH];
  guint64       skews[SKEWS_ARRAY_LENGTH];
  GstClockTime  skews_arrived[SKEWS_ARRAY_LENGTH];
  guint8        skews_index;
  GstClockTime  last_median;


};

struct _MpRTPReceiverPathClass
{
  GObjectClass parent_class;
};

struct _MpRTPRPacket{
  GstBuffer *buffer;
  guint16  seq_num;
  gboolean frame_start;
  gboolean frame_end;
};


GType mprtpr_path_get_type (void);
void mprtpr_path_destroy(gpointer ptr);
MpRTPRPath *make_mprtpr_path (guint8 id);
//guint64 mprtpr_path_get_packet_skew_median (MPRTPRPath * this);

guint16 mprtpr_path_get_cycle_num(MpRTPRPath * this);
guint16 mprtpr_path_get_highest_sequence_number(MpRTPRPath * this);
guint32 mprtpr_path_get_jitter(MpRTPRPath * this);
guint32 mprtpr_path_get_total_packet_losts_num (MpRTPRPath * this);
guint32 mprtpr_path_get_total_late_discarded_num(MpRTPRPath * this);
guint32 mprtpr_path_get_total_late_discarded_bytes_num(MpRTPRPath * this);
guint32 mprtpr_path_get_total_bytes_received (MpRTPRPath * this);
guint32 mprtpr_path_get_total_payload_bytes (MpRTPRPath * this);
guint32 mprtpr_path_get_total_duplicated_packet_num(MpRTPRPath * this);
guint64 mprtpr_path_get_total_received_packets_num (MpRTPRPath * this);
guint32 mprtpr_path_get_total_early_discarded_packets_num (MpRTPRPath * this);
guint8 mprtpr_path_get_id (MpRTPRPath * this);

void mprtpr_path_process_rtp_packet(MpRTPRPath *this,
                               GstRTPBuffer *rtp,
                               guint16 packet_subflow_seq_num,
                               guint64 snd_time);

void mprtpr_path_removes_obsolate_packets(MpRTPRPath *this, GstClockTime treshold);
guint64 mprtpr_path_get_last_skew(MpRTPRPath *this);
void mprtpr_path_playout_tick(MpRTPRPath *this);
guint64 mprtpr_path_get_drift_window(MpRTPRPath *this);
guint32 mprtpr_path_get_skew_byte_num(MpRTPRPath *this);
guint32 mprtpr_path_get_skew_packet_num(MpRTPRPath *this);


G_END_DECLS
#endif /* MPRTPRSUBFLOW_H_ */
