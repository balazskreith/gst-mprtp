
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
static void _add_delay(MpRTPRPath *this, GstClockTime delay);
static gint64 _get_drift_window (MpRTPRPath * this);
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
  this->lt_low_delays = make_percentiletracker(1024, 40);
  percentiletracker_set_treshold(this->lt_low_delays, 30 * GST_SECOND);
  this->lt_high_delays = make_percentiletracker(1024, 80);
  percentiletracker_set_treshold(this->lt_high_delays, 30 * GST_SECOND);
  this->skews = make_percentiletracker(100, 50);
  percentiletracker_set_treshold(this->skews, 2 * GST_SECOND);
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
  g_object_unref(this->lt_low_delays);
  g_object_unref(this->skews);
}


void
mprtpr_path_reset (MpRTPRPath * this)
{
  this->seq_initialized = FALSE;
  //this->skew_initialized = FALSE;
  this->cycle_num = 0;
  this->total_late_discarded = 0;
  this->total_late_discarded_bytes = 0;
  this->highest_seq = 0;
  this->jitter = 0;
  this->total_packet_losts = 0;
  this->total_packets_received = 0;
  this->total_payload_bytes = 0;

  this->last_packet_skew = 0;
  this->last_received_time = 0;

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

void mprtpr_path_get_RR_stats(MpRTPRPath *this,
                           guint16 *HSN,
                           guint16 *cycle_num,
                           guint32 *jitter,
                           guint32 *received_num,
                           guint32 *received_bytes)
{
  THIS_READLOCK (this);
  if(HSN) *HSN = this->highest_seq;
  if(cycle_num) *cycle_num = this->cycle_num;
  if(jitter) *jitter = this->jitter;
  if(received_num) *received_num = this->total_packets_received;
  if(received_bytes) *received_bytes = this->total_payload_bytes;
  THIS_READUNLOCK (this);
}

void mprtpr_path_get_XR7243_stats(MpRTPRPath *this,
                           guint16 *discarded,
                           guint32 *discarded_bytes)
{
  THIS_READLOCK (this);
  if(discarded) *discarded = this->total_late_discarded;
  if(discarded_bytes) *discarded_bytes = this->total_late_discarded_bytes;
  THIS_READUNLOCK (this);
}

void mprtpr_path_get_FBCC_stats(MpRTPRPath *this,
                           GstClockTime *sh_last_delay,
                           GstClockTime *md_last_delay,
                           GstClockTime *lt_40th_delay,
                           GstClockTime *lt_80th_delay)
{
  THIS_READLOCK (this);
  if(sh_last_delay) *sh_last_delay = this->sh_delay;
  if(md_last_delay) *md_last_delay = this->md_delay;
  if(lt_40th_delay) *lt_40th_delay = percentiletracker_get_stats(this->lt_low_delays, NULL, NULL, NULL);
  if(lt_80th_delay) *lt_80th_delay = percentiletracker_get_stats(this->lt_high_delays, NULL, NULL, NULL);
  THIS_READUNLOCK (this);
}

void mprtpr_path_get_joiner_stats(MpRTPRPath *this,
                           GstClockTime *md_last_delay,
                           gdouble       *path_skew,
                           guint32       *jitter)
{
  THIS_READLOCK (this);
  if(md_last_delay) *md_last_delay = this->sh_delay;
//  if(skew) *skew = _get_drift_window(this);
  if(path_skew) *path_skew = this->path_skew;
  if(jitter) *jitter = this->jitter;
//  g_print("%d: %f\n", this->id, *path_skew);
  THIS_READUNLOCK (this);
}

void mprtpr_path_add_discard(MpRTPRPath *this, GstMpRTPBuffer *mprtp)
{
  THIS_WRITELOCK (this);
//  g_print("Discarded on subflow %d\n", this->id);
  ++this->total_late_discarded;
  this->total_late_discarded_bytes+=mprtp->payload_bytes;
  THIS_WRITEUNLOCK (this);
}

void mprtpr_path_add_delay(MpRTPRPath *this, GstClockTime delay)
{
  THIS_WRITELOCK (this);
  _add_delay(this, delay);
  THIS_WRITEUNLOCK (this);
}


gint64
_get_drift_window (MpRTPRPath * this)
{
  guint64 skew;
  skew = percentiletracker_get_stats(this->skews, NULL, NULL, NULL);
  if((skew & ((guint64)1<<63)) == 0){
    return (gint64)-1 * ((gint64) skew);
  }else{
    return (skew ^ ((guint64)1<<63));
  }
}



void
mprtpr_path_process_rtp_packet (MpRTPRPath * this, GstMpRTPBuffer *mprtp)
{
//  g_print("mprtpr_path_process_rtp_packet begin\n");
  gint64 skew;
  guint64 uskew;

  THIS_WRITELOCK (this);
  gst_mprtp_buffer_read_map(mprtp);
  if (this->seq_initialized == FALSE) {
    this->highest_seq = mprtp->subflow_seq;
    this->total_packets_received = 1;
    this->last_rtp_timestamp = gst_mprtp_ptr_buffer_get_timestamp(mprtp);
    this->last_mprtp_delay = mprtp->delay;
    _add_delay(this, mprtp->delay);
    this->seq_initialized = TRUE;
    goto done;
  }

  skew = ((gint64)mprtp->delay) - ((gint64)this->last_mprtp_delay);
  this->last_mprtp_delay = mprtp->delay;
  ++this->total_packets_received;
  this->total_payload_bytes += mprtp->payload_bytes;
  this->jitter += ((skew < 0?-1*skew:skew) - this->jitter) / 16;
//  g_print("J: %d\n", this->jitter);
  _add_delay(this, mprtp->delay);

  if(_cmp_seq(this->highest_seq, mprtp->subflow_seq) >= 0){
    goto done;
  }
  this->highest_seq = mprtp->subflow_seq;
  if(this->last_rtp_timestamp == gst_mprtp_ptr_buffer_get_timestamp(mprtp))
    goto done;
  if(skew < 0) percentiletracker_add(this->skews, ~(skew-1));
  else         percentiletracker_add(this->skews, skew | (1UL<<63));

  uskew = percentiletracker_get_stats(this->skews, NULL, NULL, NULL);
  if((uskew & (3UL<<62)) > 0){
      uskew = uskew & 0x3FFFFFFFFFFFFFFFUL;
    this->path_skew = this->path_skew * .99 + (gdouble)uskew * .01;
  }
  else{
    this->path_skew = this->path_skew * .99 - (gdouble)uskew * .01;
  }
//  g_print("S%d: %f\n", this->id, this->path_skew);
  //new frame
  if(0) _get_drift_window(this);
  this->last_rtp_timestamp = gst_mprtp_ptr_buffer_get_timestamp(mprtp);

done:
  gst_mprtp_buffer_read_unmap(mprtp);
  THIS_WRITEUNLOCK (this);
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


void _add_delay(MpRTPRPath *this, GstClockTime delay)
{
  percentiletracker_add(this->lt_low_delays, delay);
  percentiletracker_add(this->lt_high_delays, delay);
  if(this->seq_initialized)
    this->md_delay = (31 * this->md_delay + delay ) / 32;
  else
    this->md_delay = delay;
  if(delay < this->last_delay_a){
    if(this->last_delay_a < this->last_delay_b)
      this->sh_delay = this->last_delay_a;
    else if(this->last_delay_b < delay)
      this->sh_delay = delay;
    else
      this->sh_delay = this->last_delay_b;
  }else{
    if(delay < this->last_delay_b)
      this->sh_delay = delay;
    else if(this->last_delay_a < this->last_delay_b)
      this->sh_delay = this->last_delay_a;
    else
      this->sh_delay = this->last_delay_b;
  }
  this->last_delay_b = this->last_delay_a;
  this->last_delay_a = delay;
}

#undef THIS_READLOCK
#undef THIS_READUNLOCK
#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
