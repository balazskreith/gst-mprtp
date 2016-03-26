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

G_BEGIN_DECLS

typedef struct _MpRTPReceiverPath MpRTPRPath;
typedef struct _MpRTPReceiverPathClass MpRTPRPathClass;
typedef struct _MpRTPRReceivedItem  MpRTPRReceivedItem;



#define MPRTPR_PACKET_INIT           {NULL, 0, 0, 0}

#define MPRTPR_PATH_TYPE             (mprtpr_path_get_type())
#define MPRTPR_PATH(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),MPRTPR_PATH_TYPE,MPRTPRPath))
#define MPRTPR_PATH_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),MPRTPR_PATH_TYPE,MPRTPRPathClass))
#define MPRTPR_PATH_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),MPRTPR_PATH_TYPE))
#define MPRTPR_PATH_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),MPRTPR_PATH_TYPE))
#define MPRTPR_PATH_CAST(src)        ((MpRTPRPath *)(src))

#define SKEWS_ARRAY_LENGTH 256

typedef struct _DiscardRunningLengthEncodingBlock DiscardRLEBlock;
struct _DiscardRunningLengthEncodingBlock{
  guint16      start_seq;
  guint16      end_seq;
  guint16      discarded_packets;
  guint16      discarded_bytes;
};

typedef struct _DiscardRunningLengthEncoding DiscardRLE;
struct _DiscardRunningLengthEncoding{
  DiscardRLEBlock     blocks[MPRTP_PLUGIN_MAX_RLE_LENGTH];
  guint               write_index;
  guint               read_index;
  GstClockTime        last_step;
  GstClockTime        step_interval;
};

typedef struct _LostsRunningLengthEncodingBlock LostsRLEBlock;
struct _LostsRunningLengthEncodingBlock{
  guint16      start_seq;
  guint16      end_seq;
  guint16      lost_packets;
  guint16      lost_bytes;
};

typedef struct _LostsRunningLengthEncoding LostsRLE;
struct _LostsRunningLengthEncoding{
  LostsRLEBlock       blocks[MPRTP_PLUGIN_MAX_RLE_LENGTH];
  guint               write_index;
  guint               read_index;
  GstClockTime        last_step;
  GstClockTime        step_interval;
};

typedef struct _OWDRunningLengthEncodingBlock OWDRLEBlock;
struct _OWDRunningLengthEncodingBlock{
  guint16      start_seq;
  guint16      end_seq;
  GstClockTime median_delay;
};

typedef struct _OWDRunningLengthEncoding OWDRLE;
struct _OWDRunningLengthEncoding{
  OWDRLEBlock         blocks[MPRTP_PLUGIN_MAX_RLE_LENGTH];
  guint               write_index;
  guint               read_index;
  GstClockTime        last_step;
  GstClockTime        step_interval;
};


struct _MpRTPReceiverPath
{
  GObject             object;
  guint8              id;
  GRWLock             rwmutex;
  GstClock*           sysclock;

  gboolean            seq_initialized;
  guint16             cycle_num;
  guint32             total_late_discarded;
  guint32             interval_late_discarded;
  guint32             total_late_discarded_bytes;
  guint32             interval_late_discarded_bytes;
  guint32             total_payload_bytes;
  gint32              jitter;
  guint16             highest_seq;
  guint16             reported_sequence_number;
  GstClockTime        discard_latency;

  guint64             ext_rtptime;
  guint64             last_packet_skew;
  GstClockTime        last_received_time;

  OWDRLE              owd_rle;
  LostsRLE            losts_rle;
  DiscardRLE          discard_rle;

  guint32             total_packet_losts;
  guint32             total_packets_received;
  guint32             last_rtp_timestamp;


  gdouble             path_skew;
  GstClockTime        last_mprtp_delay;
  PercentileTracker*  delays;
  PercentileTracker2* skews;

  gdouble             delay_avg;
  gboolean            request_urgent_report;

  NumsTracker*        gaps;
  NumsTracker*        lates;
  gboolean            distorted;


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

guint8 mprtpr_path_get_id (MpRTPRPath * this);
void mprtpr_path_get_RR_stats(MpRTPRPath *this,
                              guint16 *HSN,
                              guint16 *cycle_num,
                              guint32 *jitter,
                              guint32 *received_num,
                              guint32 *total_lost,
                              guint32 *received_bytes);

void mprtpr_path_set_reported_sequence(
    MpRTPRPath *this,
    guint16 sequence_number);

void mprtpr_path_get_total_discards (MpRTPRPath * this,
                               guint16 *discarded,
                               guint32 *discarded_bytes);

void mprtpr_path_get_XR7243_stats(MpRTPRPath *this,
                           guint16 *discarded,
                           guint32 *discarded_bytes);

void mprtpr_path_get_XROWD_stats(MpRTPRPath *this,
                                 GstClockTime *median,
                                 GstClockTime *min,
                                 GstClockTime* max);
gboolean
mprtpr_path_request_urgent_report(MpRTPRPath *this);

void
mprtpr_path_set_chunks_reported(MpRTPRPath *this);

void
mprtpr_path_set_discard_latency(MpRTPRPath *this, GstClockTime latency);

void
mprtpr_path_set_lost_latency(MpRTPRPath *this, GstClockTime latency);

GstRTCPXR_Chunk *
mprtpr_path_get_XR7097_packet_nums_chunks(MpRTPRPath *this,
                              guint *chunks_num,
                              guint16 *begin_seq,
                              guint16 *end_seq);

//#define mprtpr_path_get_XR7097_packet_nums_chunks(this, chunks_num, begin_seq, end_seq) mprtpr_path_get_chunks(this, 0, chunks_num, begin_seq, end_seq)
//#define mprtpr_path_get_XR7097_sum_bytes_chunks(this, chunks_num, begin_seq, end_seq) mprtpr_path_get_chunks(this, 3, chunks_num, begin_seq, end_seq)
//#define mprtpr_path_get_owd_chunks(this, chunks_num, begin_seq, end_seq) mprtpr_path_get_chunks(this, 1, chunks_num, begin_seq, end_seq)
//#define mprtpr_path_get_XR3611_chunks(this, chunks_num, begin_seq, end_seq) mprtpr_path_get_chunks(this, 2, chunks_num, begin_seq, end_seq)

GstRTCPXR_Chunk *
mprtpr_path_get_lost_chunks(MpRTPRPath *this,
                              guint *chunks_num,
                              guint16 *begin_seq,
                              guint16 *end_seq);

GstRTCPXR_Chunk *
mprtpr_path_get_owd_chunks(MpRTPRPath *this,
                              guint *chunks_num,
                              guint16 *begin_seq,
                              guint16 *end_seq,
                              guint32 *offset);

GstRTCPXR_Chunk *
mprtpr_path_get_discard_chunks(MpRTPRPath *this,
                              guint *chunks_num,
                              guint16 *begin_seq,
                              guint16 *end_seq);

void mprtpr_path_get_joiner_stats(MpRTPRPath *this,
                           gdouble       *path_delay,
                           gdouble       *path_skew,
                           guint32       *jitter);

gboolean mprtpr_path_is_distorted(MpRTPRPath *this);
void mprtpr_path_tick(MpRTPRPath *this);
void mprtpr_path_add_discard(MpRTPRPath *this, GstMpRTPBuffer *mprtp);
void mprtpr_path_add_delay(MpRTPRPath *this, GstClockTime delay);

void mprtpr_path_process_rtp_packet(MpRTPRPath *this,
                                    GstMpRTPBuffer *mprtp);

G_END_DECLS
#endif /* MPRTPRSUBFLOW_H_ */
