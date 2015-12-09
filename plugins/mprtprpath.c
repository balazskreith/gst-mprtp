
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/rtp/gstrtpbuffer.h>
#include <gst/rtp/gstrtcpbuffer.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
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
static void _add_skew(MpRTPRPath *this, gint64 skew);
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
  this->delay_estimator = make_skalmanfilter_full(1024, GST_SECOND, .25);
  this->skew_estimator = make_skalmanfilter_full(1024, GST_SECOND, .125);
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
  if(sh_last_delay) *sh_last_delay = 0;
  if(md_last_delay) *md_last_delay = 0;
  if(lt_40th_delay) *lt_40th_delay = percentiletracker_get_stats(this->lt_low_delays, NULL, NULL, NULL);
  if(lt_80th_delay) *lt_80th_delay = percentiletracker_get_stats(this->lt_high_delays, NULL, NULL, NULL);
  THIS_READUNLOCK (this);
}

void mprtpr_path_get_joiner_stats(MpRTPRPath *this,
                           gdouble       *path_delay,
                           gdouble       *path_skew,
                           guint32       *jitter)
{
  THIS_READLOCK (this);
  if(path_delay) *path_delay = this->estimated_delay;
  if(path_skew) *path_skew = this->estimated_skew;
  if(jitter) *jitter = this->jitter;
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



void
mprtpr_path_process_rtp_packet (MpRTPRPath * this, GstMpRTPBuffer *mprtp)
{
//  g_print("mprtpr_path_process_rtp_packet begin\n");
  gint64 skew;

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

  _add_skew(this, skew);

  //For Kalman delay and skew estimation test (kalman_simple_test)
//  if(this->id == 1)
//    g_print("%lu,%lu,%lu,%lu,%ld,%f,%f\n",
//            GST_TIME_AS_USECONDS((guint64)mprtp->delay),
//            GST_TIME_AS_USECONDS((guint64)this->sh_delay),
//            GST_TIME_AS_USECONDS((guint64)this->md_delay),
//            GST_TIME_AS_USECONDS((guint64)this->estimated_delay),
//            skew / 1000,
//            this->path_skew / 1000.,
//            this->estimated_skew / 1000.
//            );

  //new frame
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
  this->estimated_delay = skalmanfilter_measurement_update(this->delay_estimator, delay);
  this->md_delay = ((gdouble)this->md_delay * 31 + (gdouble) delay) / 32.;
  this->sh_delay = ((gdouble)this->sh_delay * 3 + (gdouble) delay) / 4.;
}

void _add_skew(MpRTPRPath *this, gint64 skew)
{
  this->estimated_skew = skalmanfilter_measurement_update(this->skew_estimator, skew);
}

#undef THIS_READLOCK
#undef THIS_READUNLOCK
#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
