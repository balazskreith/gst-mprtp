
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

static void _balancing_skew_trees (MpRTPRPath * this);
static void _add_delay(MpRTPRPath *this, GstClockTime delay);
static void _balancing_delay_trees (MpRTPRPath * this);
static void _add_skew(MpRTPRPath *this, guint64 skew, guint8 payload_octets);
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
  this->packetsqueue = make_packetsqueue();
  this->min_skew_bintree = make_bintree(_cmp_for_min);
  this->max_skew_bintree = make_bintree(_cmp_for_max);
  this->min_delay_bintree = make_bintree(_cmp_for_min);
  this->max_delay_bintree = make_bintree(_cmp_for_max);
  this->last_drift_window = GST_MSECOND;
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
  g_object_unref(this->min_skew_bintree);
  g_object_unref(this->max_skew_bintree);
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
  this->jitter = 0;
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
  this->played_highest_seq = 0;

  this->skew_bytes = 0;
  this->skews_index = 0;
  memset(this->skews, 0, sizeof(SKEWS_ARRAY_LENGTH) * sizeof(guint64));
  memset(this->skews_payload_octets, 0, sizeof(SKEWS_ARRAY_LENGTH) * sizeof(guint8));
  memset(this->skews_arrived, 0, sizeof(SKEWS_ARRAY_LENGTH) * sizeof(GstClockTime));

  memset(this->delays, 0, sizeof(SKEWS_ARRAY_LENGTH) * sizeof(GstClockTime));
  memset(this->delays_arrived, 0, sizeof(SKEWS_ARRAY_LENGTH) * sizeof(GstClockTime));
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
  result = this->jitter;
  THIS_READUNLOCK (this);
  return result;
}

guint32
mprtpr_path_get_total_packet_losts_num (MpRTPRPath * this)
{
  guint16 result;
  THIS_READLOCK (this);
  result = this->total_packet_losts;
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


guint32
mprtpr_path_get_total_duplicated_packet_num (MpRTPRPath * this)
{
  guint16 result;
  THIS_READLOCK (this);
  result = this->total_duplicated_packet_num;
  THIS_READUNLOCK (this);
  return result;
}


guint32
mprtpr_path_get_total_early_discarded_packets_num (MpRTPRPath * this)
{
  guint32 result;
  THIS_READLOCK (this);
  result = this->total_early_discarded;
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
  while(packetsqueue_head_obsolted(this->packetsqueue, obsolate_margin)){
    packetsqueue_remove_head(this->packetsqueue, NULL);
  }
//  g_print("Obsolate next: %p - %d packets\n", next, num);
  _balancing_skew_trees (this);
  THIS_WRITEUNLOCK (this);
//  g_print("mprtpr_path_removes_obsolate_packets end\n");
}

guint32 mprtpr_path_get_skew_byte_num(MpRTPRPath *this)
{
  guint32 result;
  THIS_READLOCK (this);
  result = this->skew_bytes;
  THIS_READUNLOCK (this);
  return result;
}

guint32 mprtpr_path_get_skew_packet_num(MpRTPRPath *this)
{
  guint32 result;
  THIS_READLOCK (this);
  result = bintree_get_num(this->min_skew_bintree) + bintree_get_num(this->max_skew_bintree);
  THIS_READUNLOCK (this);
  return result;
}

void mprtpr_path_set_state(MpRTPRPath *this, MPRTPRPathState state)
{
  THIS_WRITELOCK (this);
  this->state = state;
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

guint64 mprtpr_path_get_last_skew(MpRTPRPath *this)
{
  guint64 result = 0;
  guint8 last_index = 0;
//  g_print("mprtpr_path_get_last_skew begin\n");
  THIS_READLOCK (this);
  last_index = this->skews_index-1;
  result = this->skews[last_index];
  THIS_READUNLOCK (this);
//  g_print("mprtpr_path_get_last_skew end\n");
  return result;
}

void mprtpr_path_playout_tick(MpRTPRPath *this)
{
//  g_print("mprtpr_path_playout_tick begin\n");
  THIS_WRITELOCK (this);
  this->played_highest_seq = this->highest_seq;
  THIS_WRITEUNLOCK (this);
//  g_print("mprtpr_path_playout_tick end\n");
}


void mprtpr_path_set_played_highest_seq(MpRTPRPath *this, guint16 played_highest_seq)
{
  THIS_WRITELOCK (this);
  this->played_highest_seq = played_highest_seq;
  THIS_WRITEUNLOCK (this);
}

guint64
mprtpr_path_get_drift_window (MpRTPRPath * this,
                              GstClockTime *min_skew,
                              GstClockTime *max_skew)
{
  guint64 result;
  gint32 max_count, min_count;
//  g_print("mprtpr_path_get_skew_median begin\n");
  THIS_WRITELOCK (this);
  result = this->last_drift_window;
  min_count = bintree_get_num(this->min_skew_bintree);
  max_count = bintree_get_num(this->max_skew_bintree);
  if(min_count + max_count < 3)
    goto done;
  if(min_count < max_count)
    result = bintree_get_top_value(this->max_skew_bintree);
  else if(max_count < min_count)
    result = bintree_get_top_value(this->min_skew_bintree);
  else{
      result = (bintree_get_top_value(this->max_skew_bintree) +
                bintree_get_top_value(this->min_skew_bintree))>>1;
  }
//  g_print("%d-%d\n", min_count, max_count);
  if(min_skew) *min_skew = bintree_get_bottom_value(this->max_skew_bintree);
  if(max_skew) *max_skew = bintree_get_bottom_value(this->min_skew_bintree);
done:
  this->last_drift_window = result;
  THIS_WRITEUNLOCK (this);
//  g_print("mprtpr_path_get_skew_median end\n");
//  g_print("MEDIAN: %lu\n", result);
  return result;
}


GstClockTime
mprtpr_path_get_delay (MpRTPRPath * this, GstClockTime *min_delay,
                       GstClockTime *max_delay)
{
  guint64 result;
  gint32 max_count, min_count;
//  g_print("mprtpr_path_get_delay_median begin\n");
  THIS_WRITELOCK (this);
  result = this->last_delay;
  if(min_delay) *min_delay = result;
  if(max_delay) *max_delay = result;
  min_count = bintree_get_num(this->min_delay_bintree);
  max_count = bintree_get_num(this->max_delay_bintree);
  if(min_count + max_count < 3)
    goto done;
  if(min_count < max_count)
    result = bintree_get_top_value(this->max_delay_bintree);
  else if(max_count < min_count)
    result = bintree_get_top_value(this->min_delay_bintree);
  else{
      result = (bintree_get_top_value(this->max_delay_bintree) +
                bintree_get_top_value(this->min_delay_bintree))>>1;
  }
  if(min_delay) *min_delay = bintree_get_bottom_value(this->max_delay_bintree);
  if(max_delay) *max_delay = bintree_get_bottom_value(this->min_delay_bintree);
//  g_print("%d-%d\n", min_count, max_count);
done:
  this->last_delay = result;
  THIS_WRITEUNLOCK (this);
//  g_print("mprtpr_path_get_delay_median end\n");
//  g_print("MEDIAN: %lu\n", result);
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
    this->played_highest_seq = packet_subflow_seq_num;
    this->total_packets_received = 1;
    this->seq_initialized = TRUE;
    packetsqueue_add(this->packetsqueue,
                     snd_time,
                     packet_subflow_seq_num,
                     &delay);
    goto done;
  }

  //calculate lost, discarded and received packets
  payload_bytes = gst_rtp_buffer_get_payload_len(rtp);
  ++this->total_packets_received;
  this->total_payload_bytes += payload_bytes;

  if (packet_subflow_seq_num == (guint16) (this->highest_seq + 1)) {
    ++this->highest_seq;
    goto add;
  }
  if (_cmp_seq (this->highest_seq, packet_subflow_seq_num) < 0) {
    packetsqueue_prepare_gap(this->packetsqueue);
    this->highest_seq = packet_subflow_seq_num;
    goto add;
  }

  if (_cmp_seq (this->highest_seq, packet_subflow_seq_num) > 0) {
      packetsqueue_prepare_discarded(this->packetsqueue);
      if (_cmp_seq (this->played_highest_seq, packet_subflow_seq_num) > 0) {
        ++this->total_late_discarded;
        this->total_late_discarded_bytes += payload_bytes;
      }
      goto inspect;
  }

inspect:
//  g_print("INSPECTING\n");
  {
    gboolean duplicated;
    gboolean found;
    found = packetsqueue_try_found_a_gap(this->packetsqueue, packet_subflow_seq_num, &duplicated);
    if(!found) ++this->total_packet_losts;
    else if(duplicated) ++this->total_duplicated_packet_num;
  }
add:
//  g_print("ADDING\n");
  skew = packetsqueue_add(this->packetsqueue,
                          snd_time,
                          packet_subflow_seq_num, &delay);

  if(this->last_rtp_timestamp != gst_rtp_buffer_get_timestamp(rtp)){
    _add_skew(this, skew, payload_bytes>>3);
    this->last_rtp_timestamp = gst_rtp_buffer_get_timestamp(rtp);
  }
done:
  _add_delay(this, delay);
  THIS_WRITEUNLOCK (this);
//  g_print("mprtpr_path_process_rtp_packet end\n");
  return;

}

void _add_skew(MpRTPRPath *this, guint64 skew, guint8 payload_octets)
{
  GstClockTime treshold,now,added;
//  g_print("Subflow %d added skew: %lu\n", this->id, skew);
  now = gst_clock_get_time(this->sysclock);
  treshold = now - 2 * GST_SECOND;
  again:
  ++this->skews_index;
  //elliminate the old ones
//  g_print("Subflow %d current byte num: %u payload bytes: %u\n", this->id, this->skew_bytes, );
  this->skew_bytes -= this->skews_payload_octets[this->skews_index]<<3;
  this->skews_payload_octets[this->skews_index] = 0;
  if(this->skews[this->skews_index] > 0){
    if(this->skews[this->skews_index] <= bintree_get_top_value(this->max_skew_bintree))
      bintree_delete_value(this->max_skew_bintree, this->skews[this->skews_index]);
    else
      bintree_delete_value(this->min_skew_bintree, this->skews[this->skews_index]);
  }
  this->skews[this->skews_index] = 0;
  added = this->skews_arrived[this->skews_index];
  this->skews_arrived[this->skews_index] = 0;
  if(0 < added && added < treshold) goto again;

  //add new one
  this->skews[this->skews_index] = skew;
  this->skews_arrived[this->skews_index] = now;
  this->skew_bytes += (guint32)payload_octets<<3;
  this->skews_payload_octets[this->skews_index] = payload_octets;
  if(this->skews[this->skews_index] <= bintree_get_top_value(this->max_skew_bintree))
    bintree_insert_value(this->max_skew_bintree, this->skews[this->skews_index]);
  else
    bintree_insert_value(this->min_skew_bintree, this->skews[this->skews_index]);

  _balancing_skew_trees(this);

  ++this->skews_index;

}

void
_balancing_skew_trees (MpRTPRPath * this)
{
  gint32 max_count, min_count;
  gint32 diff;
  BinTreeNode *top;


balancing:
  min_count = bintree_get_num(this->min_skew_bintree);
  max_count = bintree_get_num(this->max_skew_bintree);

  //  diff = (max_count>>1) - min_count;
  diff = (max_count) - min_count;
//  g_print("max_tree_num: %d, min_tree_num: %d\n", max_tree_num, min_tree_num);
  if (-2 < diff && diff < 2) {
    goto done;
  }
  if (diff < -1) {
    top = bintree_pop_top_node(this->min_skew_bintree);
    bintree_insert_node(this->max_skew_bintree, top);
  } else if (1 < diff) {
      top = bintree_pop_top_node(this->max_skew_bintree);
      bintree_insert_node(this->min_skew_bintree, top);
  }
  goto balancing;

done:
  return;
}



void _add_delay(MpRTPRPath *this, GstClockTime delay)
{
  GstClockTime treshold,now,added;
//  g_print("Subflow %d added delay: %lu\n", this->id, delay);
  now = gst_clock_get_time(this->sysclock);
  treshold = now - 2 * GST_SECOND;
  again:
  ++this->delays_index;
  //elliminate the old ones
  if(this->delays[this->delays_index] > 0){
    if(this->delays[this->delays_index] <= bintree_get_top_value(this->max_delay_bintree))
      bintree_delete_value(this->max_delay_bintree, this->delays[this->delays_index]);
    else
      bintree_delete_value(this->min_delay_bintree, this->delays[this->delays_index]);
  }
  this->delays[this->delays_index] = 0;
  added = this->delays_arrived[this->delays_index];
  this->delays_arrived[this->delays_index] = 0;
  if(0 < added && added < treshold) goto again;

  //add new one
  this->delays[this->delays_index] = delay;
  this->delays_arrived[this->delays_index] = now;
  if(this->delays[this->delays_index] <= bintree_get_top_value(this->max_delay_bintree))
    bintree_insert_value(this->max_delay_bintree, this->delays[this->delays_index]);
  else
    bintree_insert_value(this->min_delay_bintree, this->delays[this->delays_index]);

  _balancing_delay_trees(this);

  ++this->delays_index;

}

void
_balancing_delay_trees (MpRTPRPath * this)
{
  gint32 max_count, min_count;
  gint32 diff;
  BinTreeNode *top;


balancing:
  min_count = bintree_get_num(this->min_delay_bintree);
  max_count = bintree_get_num(this->max_delay_bintree);

 //To get the 75 percentile we shift max_count by 1
  diff = (max_count>>1) - min_count;
//  g_print("max_tree_num: %d, min_tree_num: %d\n", max_tree_num, min_tree_num);
  if (-2 < diff && diff < 2) {
    goto done;
  }
  if (diff < -1) {
    top = bintree_pop_top_node(this->min_delay_bintree);
    bintree_insert_node(this->max_delay_bintree, top);
  } else if (1 < diff) {
      top = bintree_pop_top_node(this->max_delay_bintree);
      bintree_insert_node(this->min_delay_bintree, top);
  }
  goto balancing;

done:
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
