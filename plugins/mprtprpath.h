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

G_BEGIN_DECLS

typedef struct _MpRTPReceiverPath MpRTPRPath;
typedef struct _MpRTPReceiverPathClass MpRTPRPathClass;
typedef struct _MpRTPRReceivedItem  MpRTPRReceivedItem;
typedef struct _SkewTree SkewTree;
typedef struct _SkewChain PacketChain;


#define MPRTPR_PATH_TYPE             (mprtpr_path_get_type())
#define MPRTPR_PATH(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),MPRTPR_PATH_TYPE,MPRTPRPath))
#define MPRTPR_PATH_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),MPRTPR_PATH_TYPE,MPRTPRPathClass))
#define MPRTPR_PATH_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),MPRTPR_PATH_TYPE))
#define MPRTPR_PATH_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),MPRTPR_PATH_TYPE))
#define MPRTPR_PATH_CAST(src)        ((MpRTPRPath *)(src))



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

  guint32       received_since_cycle_is_increased;
  guint32       total_late_discarded;
  guint32       total_late_discarded_bytes;
  guint32       total_received_bytes;
  guint32       total_early_discarded;
  guint32       total_duplicated_packet_num;
  guint16       actual_seq;

  guint64       ext_rtptime;
  guint64       last_packet_skew;
  GstClockTime  last_received_time;
  GstClock*     sysclock;
  guint16       HSN;
  guint32       total_packet_losts;
  guint64       total_packet_received;

  SkewTree*     skew_max_tree;
  SkewTree*     skew_min_tree;
  PacketChain*  packet_chain;
  GQueue*       packets_pool;
  GstQueueArray*       skews_pool;
  GQueue*       gaps_pool;
  GstClockTime  last_median;


};

struct _MpRTPReceiverPathClass
{
  GObjectClass parent_class;
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
guint32 mprtpr_path_get_total_duplicated_packet_num(MpRTPRPath * this);
guint64 mprtpr_path_get_total_received_packets_num (MpRTPRPath * this);
guint32 mprtpr_path_get_total_early_discarded_packets_num (MpRTPRPath * this);
guint8 mprtpr_path_get_id (MpRTPRPath * this);

void mprtpr_path_process_rtp_packet(MpRTPRPath *this,
                               GstRTPBuffer *rtp,
                               guint16 packet_subflow_seq_num,
                               GstClockTime snd_time);

void mprtpr_path_removes_obsolate_packets(MpRTPRPath *this);
guint64 mprtpr_path_get_skew(MpRTPRPath *this);
GstBuffer* mprtpr_path_pop_buffer_to_playout(MpRTPRPath *this, guint16* abs_seq);
gboolean mprtpr_path_has_buffer_to_playout(MpRTPRPath *this);

G_END_DECLS
#endif /* MPRTPRSUBFLOW_H_ */
