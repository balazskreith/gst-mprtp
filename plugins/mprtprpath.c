
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

#define _actual_RLEBlock(this) ((RLEBlock*)(this->rle.blocks + this->rle.write_index))
#define _now(this) (gst_clock_get_time(this->sysclock))

static void mprtpr_path_finalize (GObject * object);
static void mprtpr_path_reset (MpRTPRPath * this);
static gint _cmp_seq32 (guint32 x, guint32 y);
static void _add_delay(MpRTPRPath *this, GstClockTime delay);

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
  this->spike_var_treshold = 20 * GST_MSECOND;
  this->spike_delay_treshold = 375 * GST_MSECOND;
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
//  g_object_unref(this->lt_low_delays);
//  g_object_unref(this->lt_high_delays);
//  g_object_unref(this->skews);
}


void
mprtpr_path_reset (MpRTPRPath * this)
{
  this->seq_initialized = FALSE;
  //this->skew_initialized = FALSE;
  this->cycle_num = 0;
  this->highest_seq = 0;
  this->jitter = 0;

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

guint16
mprtpr_path_get_HSSN (MpRTPRPath * this)
{
  guint16 result;
  THIS_READLOCK (this);
  result = this->highest_seq;
  THIS_READUNLOCK (this);
  return result;
}

void mprtpr_path_get_regular_stats(MpRTPRPath *this,
                              guint16 *HSN,
                              guint16 *cycle_num,
                              guint32 *jitter,
                              guint32 *received_num)
{
  THIS_READLOCK (this);
  if(HSN) *HSN = this->highest_seq;
  if(cycle_num) *cycle_num = this->cycle_num;
  if(jitter) *jitter = this->jitter;
  if(received_num) *received_num = this->total_packets_received;
  THIS_READUNLOCK (this);
}

void mprtpr_path_get_total_receivements (MpRTPRPath * this,
                                              guint32 *total_packets_received,
                                              guint32 *total_payload_received)
{
  if(total_packets_received) *total_packets_received = this->total_packets_received;
  if(total_payload_received) *total_payload_received = this->total_payload_received;
}

gboolean
mprtpr_path_is_in_spike_mode(MpRTPRPath *this)
{
  gboolean result;
  THIS_READLOCK (this);
  result = this->spike_mode;
  THIS_READUNLOCK (this);
  return result;
}

void
mprtpr_path_set_spike_treshold(MpRTPRPath *this, GstClockTime delay_treshold, GstClockTime var_treshold)
{
  THIS_WRITELOCK (this);
  this->spike_delay_treshold = delay_treshold;
  this->spike_var_treshold = var_treshold;
  THIS_WRITEUNLOCK (this);
}



void
mprtpr_path_set_owd_window_treshold(MpRTPRPath *this, GstClockTime treshold)
{
  THIS_WRITELOCK (this);

  THIS_WRITEUNLOCK (this);
}


void
mprtpr_path_set_spike_delay_treshold(MpRTPRPath *this, GstClockTime delay_treshold)
{
  THIS_WRITELOCK (this);
  this->spike_delay_treshold = delay_treshold;
  THIS_WRITEUNLOCK (this);
}

void
mprtpr_path_set_packetstracker(MpRTPRPath *this, void(*packetstracker)(gpointer,  GstMpRTPBuffer*), gpointer data)
{
  THIS_WRITELOCK(this);
  this->packetstracker = packetstracker;
  this->packetstracker_data = data;
  THIS_WRITEUNLOCK(this);
}

void
mprtpr_path_set_spike_var_treshold(MpRTPRPath *this, GstClockTime var_treshold)
{
  THIS_WRITELOCK (this);
  this->spike_var_treshold = var_treshold;
  THIS_WRITEUNLOCK (this);
}


void mprtpr_path_add_delay(MpRTPRPath *this, GstClockTime delay)
{
  THIS_WRITELOCK (this);
  _add_delay(this, delay);
  THIS_WRITEUNLOCK (this);
}

//Only process rtp packets
void
mprtpr_path_process_rtp_packet (MpRTPRPath * this, GstMpRTPBuffer *mprtp)
{
  gint64 skew;

  THIS_WRITELOCK (this);
  if(!mprtp->delay){
    GST_WARNING_OBJECT(this, "A packet delay should not be 0, the mprtpr path process doesn't work with it");
    goto done;
  }

  if (this->seq_initialized == FALSE) {
    this->highest_seq = mprtp->subflow_seq;
    this->total_packets_received = 1;
    this->total_payload_received = mprtp->payload_bytes;
    this->last_rtp_timestamp = mprtp->timestamp;
    this->last_mprtp_delay = mprtp->delay;
    _add_delay(this, mprtp->delay);
    this->seq_initialized = TRUE;
    goto done;
  }

  //normal jitter calculation for regular rtcp reports
  skew = (((gint64)this->last_mprtp_delay - (gint64)mprtp->delay));
  this->jitter += ((skew < 0?-1*skew:skew) - this->jitter) / 16;
  this->last_mprtp_delay = mprtp->delay;
  ++this->total_packets_received;
  this->total_payload_received += mprtp->payload_bytes;

  //collect and evaluate skew in another way
  if(_cmp_seq32(this->last_rtp_timestamp, mprtp->timestamp) < 0){
    this->last_rtp_timestamp = mprtp->timestamp;
  }
  _add_delay(this, mprtp->delay);

  if(this->packetstracker){
    this->packetstracker(this->packetstracker_data, mprtp);
  }

  //consider cycle num increase with allowance of a little gap
  if(65472 < this->highest_seq && mprtp->subflow_seq < 128){
    ++this->cycle_num;
  }

  //set the new packet seq as the highest seq
  this->highest_seq = mprtp->subflow_seq;

done:
  THIS_WRITEUNLOCK(this);
}

gint
_cmp_seq32 (guint32 x, guint32 y)
{
  if(x == y) return 0;
  if(x < y && y - x < 2147483648) return -1;
  if(x > y && x - y > 2147483648) return -1;
  if(x < y && y - x > 2147483648) return 1;
  if(x > y && x - y < 2147483648) return 1;
  return 0;
}


void _add_delay(MpRTPRPath *this, GstClockTime delay)
{
  GstClockTime ddelay;

  ddelay = ABS((gint64)delay - (gint64)this->last_added_delay);
  if (ddelay > this->spike_delay_treshold) {
  // A new "delay spike" has started
    this->spike_mode = TRUE;
    this->spike_var = 0;
  }else{
    if (this->spike_mode) {
      GstClockTime vdelay;
      // We're within a delay spike; maintain slope estimate
      this->spike_var = this->spike_var>>1;
      vdelay = (ABS((gint64)delay - (gint64)this->last_added_delay) +
                ABS((gint64)delay - (gint64)this->last_last_added_delay))/8;
      this->spike_var = this->spike_var + vdelay;
      if (this->spike_var < this->spike_var_treshold) {
        // Slope is flat; return to normal operation
        this->spike_mode = FALSE;
      }
    }
  }
  this->last_last_added_delay = this->last_added_delay;
  this->last_added_delay = delay;
}



#undef THIS_READLOCK
#undef THIS_READUNLOCK
#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
