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


typedef struct _MisorderedMPRTPPacket{
  guint16      seq;
  GstClockTime added;
  gboolean     received;
  gboolean     used; //indicate weather we can reuse it or not.
  guint        payload_len;
}MisorderedMPRTPPacket;

typedef struct _ExpectedMPRTPPacket{
  guint16      seq;
  GstClockTime added;
  gboolean     used; //indicate weather we can reuse it or not.
  gboolean     received;
  guint        payload_len;
  guint32      total_payload_discarded;
  guint32      total_packets_discarded;
}ExpectedMPRTPPacket;


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
  GstClockTime              discard_treshold;
  GstClockTime              lost_treshold;

  OWDRLE                    owd_rle;

  guint32                   total_packets_discarded_or_lost;
  guint32                   total_packets_lost;
  guint32                   total_packets_discarded;
  guint32                   total_payload_discarded;
  guint32                   total_packets_received;
  guint32                   total_payload_received;
  guint32                   last_rtp_timestamp;


  gdouble                   path_skew;
  gdouble                   path_delay;
  GstClockTime              last_mprtp_delay;
  PercentileTracker*        delays;
  PercentileTracker2*       skews;

  GQueue*                   misordered;
  MisorderedMPRTPPacket*    misordered_itemsbed;
  gint                      misordered_itemsbed_index;

  GQueue*                   discarded;
  MisorderedMPRTPPacket*    discarded_itemsbed;
  gint                      discarded_itemsbed_index;

  guint32                   receiver_packetrate;
  guint32                   receiver_byterate;
  guint32                   goodput_packetrate;
  guint32                   goodput_byterate;
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
guint16 mprtpr_path_get_HSSN (MpRTPRPath * this);
void mprtpr_path_get_regular_stats(MpRTPRPath *this,
                              guint16 *HSN,
                              guint16 *cycle_num,
                              guint32 *jitter,
                              guint32 *received_num,
                              guint32 *total_lost,
                              guint32 *received_bytes);

void mprtpr_path_get_total_discards (MpRTPRPath * this,
                               guint32 *total_discarded_packets,
                               guint32 *total_payload_discarded);

guint32 mprtpr_path_get_total_discarded_or_lost_packets (MpRTPRPath * this);

void mprtpr_path_get_total_receivements (MpRTPRPath * this,
                               guint32 *total_packets_received,
                               guint32 *total_payload_received);

void mprtpr_path_get_total_losts (MpRTPRPath * this,
                                  guint32 *total_packets_lost);

void mprtpr_path_get_owd_stats(MpRTPRPath *this,
                                 GstClockTime *median,
                                 GstClockTime *min,
                                 GstClockTime* max);

void
mprtpr_path_set_chunks_reported(MpRTPRPath *this);

void
mprtpr_path_set_discard_treshold(MpRTPRPath *this, GstClockTime treshold);

void
mprtpr_path_set_lost_treshold(MpRTPRPath *this, GstClockTime treshold);

void
mprtpr_path_set_owd_window_treshold(MpRTPRPath *this, GstClockTime treshold);


GstRTCPXR_Chunk *
mprtpr_path_get_owd_chunks(MpRTPRPath *this,
                              guint *chunks_num,
                              guint16 *begin_seq,
                              guint16 *end_seq,
                              guint32 *offset);



void mprtpr_path_get_joiner_stats(MpRTPRPath *this,
                           gdouble       *path_delay,
                           gdouble       *path_skew);

void mprtpr_path_get_1s_rate_stats(MpRTPRPath *this,
                                   guint32 *expected_packetsrate,
                                   guint32 *receiver_byterate,
                                   guint32 *receiver_packetrate,
                                   guint32 *goodput_byterate,
                                   guint32 *goodput_packetrate);

void mprtpr_path_tick(MpRTPRPath *this);
void mprtpr_path_add_discard(MpRTPRPath *this, GstMpRTPBuffer *mprtp);
void mprtpr_path_add_delay(MpRTPRPath *this, GstClockTime delay);

void mprtpr_path_process_rtp_packet(MpRTPRPath *this,
                                    GstMpRTPBuffer *mprtp);

G_END_DECLS
#endif /* MPRTPRSUBFLOW_H_ */
