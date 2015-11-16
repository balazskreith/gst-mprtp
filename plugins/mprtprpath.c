
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/rtp/gstrtpbuffer.h>
#include <gst/rtp/gstrtcpbuffer.h>
#include <stdio.h>
#include <string.h>
#include "mprtprpath.h"
#include "mprtpspath.h"
#include "gstmprtcpbuffer.h"
#include "streamjoiner.h"

GST_DEBUG_CATEGORY_STATIC (gst_mprtpr_path_debug_category);
#define GST_CAT_DEFAULT gst_mprtpr_path_debug_category

G_DEFINE_TYPE (MpRTPRPath, mprtpr_path, G_TYPE_OBJECT);


#define THIS_READLOCK(this) g_rw_lock_reader_lock(&this->rwmutex)
#define THIS_READUNLOCK(this) g_rw_lock_reader_unlock(&this->rwmutex)
#define THIS_WRITELOCK(this) g_rw_lock_writer_lock(&this->rwmutex)
#define THIS_WRITEUNLOCK(this) g_rw_lock_writer_unlock(&this->rwmutex)

static void mprtpr_path_finalize (GObject * object);
static void mprtpr_path_reset (MpRTPRPath * this);

static gint _cmp_seq (guint16 x, guint16 y);
static gint _cmp_for_max (guint64 x, guint64 y);
static gint _cmp_for_min (guint64 x, guint64 y);

void
mprtpr_path_class_init (MpRTPRPathClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = mprtpr_path_finalize;

  GST_DEBUG_CATEGORY_INIT (gst_mprtpr_path_debug_category, "mprtpr_path",
      0, "MPRTP Receiver Subflow");
}


MpRTPRPath *
make_mprtpr_path (guint8 id)
{
  MpRTPRPath *result;

  result = g_object_new (MPRTPR_PATH_TYPE, NULL);
  THIS_WRITELOCK (result);
  result->id = id;
  THIS_WRITEUNLOCK (result);
  return result;
}


void
mprtpr_path_init (MpRTPRPath * this)
{
  g_rw_lock_init (&this->rwmutex);
  this->sysclock = gst_system_clock_obtain ();
  this->packetsqueue = make_packetsrcvqueue();
  this->last_drift_window = GST_MSECOND;
  this->delays = make_streamtracker(_cmp_for_min, _cmp_for_max, 256, 50);
  this->skews = make_streamtracker(_cmp_for_min, _cmp_for_max, 256, 50);
  streamtracker_set_treshold(this->skews, 2 * GST_SECOND);
  mprtpr_path_reset (this);
}

void mprtpr_path_destroy(gpointer ptr)
{

  G_OBJECT_CLASS(mprtpr_path_parent_class)->finalize(G_OBJECT(ptr));
}

void
mprtpr_path_finalize (GObject * object)
{
  MpRTPRPath *this;
  this = MPRTPR_PATH_CAST (object);
  g_object_unref (this->sysclock);
  g_object_unref(this->delays);
  g_object_unref(this->skews);
  g_object_unref(this->packetsqueue);
}


void
mprtpr_path_reset (MpRTPRPath * this)
{
  this->gaps = NULL;
  this->result = NULL;
  this->seq_initialized = FALSE;
  //this->skew_initialized = FALSE;
  this->cycle_num = 0;
  this->total_late_discarded = 0;
  this->total_late_discarded_bytes = 0;
  this->total_early_discarded = 0;
  this->total_duplicated_packet_num = 0;
  this->highest_seq = 0;
  this->total_packet_losts = 0;
  this->total_packets_received = 0;
  this->total_payload_bytes = 0;

  this->ext_rtptime = -1;
  this->last_packet_skew = 0;
  this->last_received_time = 0;
  this->PHSN = 0;

}

guint16
mprtpr_path_get_cycle_num (MpRTPRPath * this)
{
  guint16 result;
  THIS_READLOCK (this);
  result = this->cycle_num;
  THIS_READUNLOCK (this);
  return result;
}

guint16
mprtpr_path_get_highest_sequence_number (MpRTPRPath * this)
{
  guint16 result;
  THIS_READLOCK (this);
  result = this->highest_seq;
  THIS_READUNLOCK (this);
  return result;
}

guint32
mprtpr_path_get_jitter (MpRTPRPath * this)
{
  guint32 result;
  THIS_READLOCK (this);
  result = packetsrcvqueue_get_jitter(this->packetsqueue);
//  g_print("Sub-%d jitter: %u\n", this->id, result);
  THIS_READUNLOCK (this);
  return result;
}

guint32
mprtpr_path_get_total_late_discarded_num (MpRTPRPath * this)
{
  guint16 result;
  THIS_READLOCK (this);
  result = this->total_late_discarded;
  THIS_READUNLOCK (this);
  return result;
}

guint32
mprtpr_path_get_total_late_discarded_bytes_num (MpRTPRPath * this)
{
  guint32 result;
  THIS_READLOCK (this);
  result = this->total_late_discarded_bytes;
  THIS_READUNLOCK (this);
  return result;
}

guint32
mprtpr_path_get_total_bytes_received (MpRTPRPath * this)
{
  guint32 result;
  THIS_READLOCK (this);
  result = this->total_payload_bytes + (28<<3) * this->total_packets_received;
  THIS_READUNLOCK (this);
  return result;
}

guint32
mprtpr_path_get_total_payload_bytes (MpRTPRPath * this)
{
  guint32 result;
  THIS_READLOCK (this);
  result = this->total_payload_bytes;
  THIS_READUNLOCK (this);
  return result;
}

guint64
mprtpr_path_get_total_received_packets_num (MpRTPRPath * this)
{
  guint32 result;
  THIS_READLOCK (this);
  result = this->total_packets_received;
  THIS_READUNLOCK (this);
  return result;
}


guint8
mprtpr_path_get_id (MpRTPRPath * this)
{
  guint8 result;
  THIS_READLOCK (this);
  result = this->id;
  THIS_READUNLOCK (this);
  return result;
}

void
mprtpr_path_removes_obsolate_packets (MpRTPRPath * this, GstClockTime treshold)
{
  GstClockTime obsolate_margin;
//  g_print("mprtpr_path_removes_obsolate_packets begin\n");
  THIS_WRITELOCK (this);
  obsolate_margin = gst_clock_get_time (this->sysclock) - treshold;
  while(packetsrcvqueue_head_obsolted(this->packetsqueue, obsolate_margin)){
    packetsrcvqueue_remove_head(this->packetsqueue, NULL);
  }
//  g_print("Obsolate next: %p - %d packets\n", next, num);
  THIS_WRITEUNLOCK (this);
//  g_print("mprtpr_path_removes_obsolate_packets end\n");
}

guint32 mprtpr_path_get_skew_byte_num(MpRTPRPath *this)
{
  guint32 result;
  THIS_READLOCK (this);
  result = 0;
  THIS_READUNLOCK (this);
  return result;
}

guint32 mprtpr_path_get_skew_packet_num(MpRTPRPath *this)
{
  guint32 result;
  THIS_READLOCK (this);
  result = 0;
  THIS_READUNLOCK (this);
  return result;
}

 void mprtpr_path_get_obsolate_stat(MpRTPRPath *this,
                                      GstClockTime treshold,
                                      guint16 *lost,
                                      guint16 *received,
                                      guint16 *expected)
{
  guint16 lost_result;
  THIS_WRITELOCK (this);

  packetsrcvqueue_get_packets_stat_for_obsolation(this->packetsqueue,
                                                   treshold,
                                                   &lost_result,
                                                   received,
                                                   expected);
  if(lost) *lost = lost_result;
  this->total_lost_packets_num += lost_result;
  THIS_WRITEUNLOCK (this);
}

void mprtpr_path_set_state(MpRTPRPath *this, MPRTPRPathState state)
{
  THIS_WRITELOCK (this);
  this->state = state;
  THIS_WRITEUNLOCK (this);
}

void mprtpr_path_set_delay(MpRTPRPath *this, GstClockTime delay)
{
  THIS_WRITELOCK (this);
  streamtracker_add(this->delays, delay);
//  g_print("Add delay to subflow %d delay %lu-num:%u-%u->index:%d\n",
//          this->id,
//          delay,
//          bintree_get_num(this->min_delay_bintree),
//          bintree_get_num(this->max_delay_bintree),
//          this->delays_write_index);
  THIS_WRITEUNLOCK (this);
}

MPRTPRPathState mprtpr_path_get_state(MpRTPRPath *this)
{
  MPRTPRPathState result;
  THIS_READLOCK (this);
  result = this->state;
  THIS_READUNLOCK (this);
  return result;
}




void mprtpr_path_set_played_seq_num(MpRTPRPath *this, guint16 played_seq_num)
{
  THIS_WRITELOCK (this);
  if(!this->PHSN) this->PHSN = played_seq_num;
  else if(_cmp_seq(this->PHSN, played_seq_num) < 0){
    this->PHSN = played_seq_num;
  }
//  g_print("Sub-%d PLAYED: %hu, PHSN: %hu\n", this->id, played_seq_num, this->played_highest_seq);
  THIS_WRITEUNLOCK (this);
}

guint64
mprtpr_path_get_drift_window (MpRTPRPath * this,
                              GstClockTime *min_skew,
                              GstClockTime *max_skew)
{
  guint64 result;
  THIS_WRITELOCK (this);
  result = this->last_drift_window;
  if(!streamtracker_get_num(this->skews)) goto done;
  result = streamtracker_get_stats(this->skews, min_skew, max_skew, NULL);
done:
  this->last_drift_window = result;
  THIS_WRITEUNLOCK (this);
  return result;
}


GstClockTime
mprtpr_path_get_delay (MpRTPRPath * this,
                       GstClockTime *min_delay,
                       GstClockTime *max_delay)
{
  guint64 result;
  THIS_WRITELOCK (this);
  result = this->last_delay;
  if(!streamtracker_get_num(this->delays)) goto done;
  result = streamtracker_get_stats(this->delays, min_delay, max_delay, NULL);
done:
  this->last_delay = result;
  THIS_WRITEUNLOCK (this);
  return result;
}

void
mprtpr_path_process_rtp_packet (MpRTPRPath * this,
    GstRTPBuffer * rtp, guint16 packet_subflow_seq_num, guint64 snd_time)
{
  guint64 skew;
  guint16 payload_bytes;
  GstClockTime delay;
//  g_print("mprtpr_path_process_rtp_packet begin\n");
  THIS_WRITELOCK (this);

  if (this->seq_initialized == FALSE) {
    this->highest_seq = packet_subflow_seq_num;
    this->total_packets_received = 1;
    this->seq_initialized = TRUE;
    packetsrcvqueue_add(this->packetsqueue,
                     snd_time,
                     packet_subflow_seq_num,
                     &delay);
    goto done;
  }

  //calculate lost, discarded and received packets
  payload_bytes = gst_rtp_buffer_get_payload_len(rtp);
  ++this->total_packets_received;
  this->total_payload_bytes += payload_bytes;
//  g_print("Sub-%d SEQ: %hu HSN: %hu\n", this->id, packet_subflow_seq_num, this->highest_seq);
  if (_cmp_seq (this->highest_seq, packet_subflow_seq_num) <= 0){
    this->highest_seq = packet_subflow_seq_num;
    goto add;
  }
  if (this->PHSN && _cmp_seq (this->PHSN, packet_subflow_seq_num) >= 0){
    ++this->total_late_discarded;
    this->total_late_discarded_bytes+=payload_bytes;
//    g_print("Sub-%d LATE %hu PHSN: %hu\n", this->id, packet_subflow_seq_num, this->PHSN);
  }

add:
  skew = packetsrcvqueue_add(this->packetsqueue,
                          snd_time,
                          packet_subflow_seq_num,
                          &delay);
//  if(this->id == 1) g_print("%lu,", delay);
  if(this->last_rtp_timestamp != gst_rtp_buffer_get_timestamp(rtp)){
    streamtracker_add(this->skews, skew);
    this->last_rtp_timestamp = gst_rtp_buffer_get_timestamp(rtp);
  }
done:
  streamtracker_add(this->delays, delay);
  THIS_WRITEUNLOCK (this);
//  g_print("mprtpr_path_process_rtp_packet end\n");
  return;

}


gint
_cmp_seq (guint16 x, guint16 y)
{

  if (x == y) {
    return 0;
  }
  /*
     if(x < y || (0x8000 < x && y < 0x8000)){
     return -1;
     }
     return 1;
   */
  return ((gint16) (x - y)) < 0 ? -1 : 1;

}

gint
_cmp_for_max (guint64 x, guint64 y)
{
  return x == y ? 0 : x < y ? -1 : 1;
}

gint
_cmp_for_min (guint64 x, guint64 y)
{
  return x == y ? 0 : x < y ? 1 : -1;
}

#undef THIS_READLOCK
#undef THIS_READUNLOCK
#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
