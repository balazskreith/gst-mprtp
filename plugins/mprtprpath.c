
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

#define MPRTPR_MISORDERED_ITEMSBED_LENGTH 1000
#define MPRTPR_DISCARDED_ITEMSBED_LENGTH 1000

#define _actual_RLEBlock(this) ((RLEBlock*)(this->rle.blocks + this->rle.write_index))
#define _now(this) (gst_clock_get_time(this->sysclock))

#define _owdrle(this) this->owd_rle
#define _actual_owdrle(this) ((OWDRLEBlock*)(this->owd_rle.blocks + this->owd_rle.write_index))


static void mprtpr_path_finalize (GObject * object);
static void mprtpr_path_reset (MpRTPRPath * this);
static gint _cmp_seq (guint16 x, guint16 y);
static gint _cmp_seq32 (guint32 x, guint32 y);
static void _add_discard(MpRTPRPath *this, guint payload_len);
static void _add_delay(MpRTPRPath *this, GstClockTime delay);
static void _add_skew(MpRTPRPath *this, gint64 skew);

static void _obsolate_discarded(MpRTPRPath *this);
static void _add_packet_to_discarded(MpRTPRPath *this, guint16 seq, guint payload_len);
static void _obsolate_misordered(MpRTPRPath *this);
static void _add_packet_to_misordered(MpRTPRPath *this, guint16 seq);
static void _find_misordered_packet(MpRTPRPath * this, guint16 seq, guint payload_len);

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
  this->delays = make_percentiletracker(512, 50);
  percentiletracker_set_treshold(this->delays, 1000 * GST_MSECOND);

  this->skews = make_percentiletracker2(100, 50);
  percentiletracker2_set_treshold(this->skews, 2 * GST_SECOND);

  _owdrle(this).last_step = _now(this);
  _owdrle(this).read_index = _owdrle(this).write_index = 0;
  _owdrle(this).step_interval = 100 * GST_MSECOND;


  this->misordered = g_queue_new();
  this->misordered_itemsbed = mprtp_malloc(sizeof(MisorderedMPRTPPacket) * MPRTPR_MISORDERED_ITEMSBED_LENGTH);
  this->discard_treshold = 300 * GST_MSECOND;

  this->discarded = g_queue_new();
  this->discarded_itemsbed = mprtp_malloc(sizeof(MisorderedMPRTPPacket) * MPRTPR_DISCARDED_ITEMSBED_LENGTH);
  this->lost_treshold = 700 * GST_MSECOND;

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
  this->total_packets_discarded = 0;
  this->total_payload_discarded = 0;
  this->highest_seq = 0;
  this->jitter = 0;
  this->total_packets_received = 0;
  this->total_payload_received = 0;
  this->total_packets_lost = 0;
  memset(this->misordered_itemsbed, 0, sizeof(MisorderedMPRTPPacket) * MPRTPR_MISORDERED_ITEMSBED_LENGTH);
  this->misordered_itemsbed_index = 0;
  memset(this->discarded_itemsbed, 0, sizeof(MisorderedMPRTPPacket) * MPRTPR_DISCARDED_ITEMSBED_LENGTH);
  this->discarded_itemsbed_index = 0;
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
                              guint32 *received_num,
                              guint32 *total_lost,
                              guint32 *received_bytes)
{
  THIS_READLOCK (this);
  if(HSN) *HSN = this->highest_seq;
  if(cycle_num) *cycle_num = this->cycle_num;
  if(jitter) *jitter = this->jitter;
  if(total_lost) *total_lost = this->total_packets_lost;
  if(received_num) *received_num = this->total_packets_received;
  if(received_bytes) *received_bytes = this->total_payload_received;
  THIS_READUNLOCK (this);
}

void mprtpr_path_get_total_discards (MpRTPRPath * this,
                               guint32 *total_discarded_packets,
                               guint32 *total_payload_discarded)
{
  THIS_READLOCK (this);
  if(total_discarded_packets) *total_discarded_packets = this->total_packets_discarded;
  if(total_payload_discarded) *total_payload_discarded = this->total_payload_discarded;
  THIS_READUNLOCK (this);
}

guint32 mprtpr_path_get_total_discarded_or_lost_packets (MpRTPRPath * this)
{
  guint32 result;
  THIS_READLOCK (this);
  result = this->total_packets_discarded_or_lost;
  THIS_READUNLOCK (this);
  return result;
}

void mprtpr_path_get_total_receivements (MpRTPRPath * this,
                               guint32 *total_packets_received,
                               guint32 *total_payload_received)
{
  THIS_READLOCK (this);
  if(total_packets_received) *total_packets_received = this->total_packets_received;
  if(total_payload_received) *total_payload_received = this->total_payload_received;
  THIS_READUNLOCK (this);
}

void mprtpr_path_get_total_losts (MpRTPRPath * this,
                               guint32 *total_packets_lost)
{
  THIS_READLOCK (this);
  if(total_packets_lost) *total_packets_lost = this->total_packets_lost;
  THIS_READUNLOCK (this);
}


void mprtpr_path_get_owd_stats(MpRTPRPath *this,
                                 GstClockTime *median,
                                 GstClockTime *min,
                                 GstClockTime* max)
{
  GstClockTime median_delay;
  THIS_READLOCK (this);
  median_delay = percentiletracker_get_stats(this->delays, min, max, NULL);
  if(median) *median = median_delay;
  THIS_READUNLOCK (this);
}


void
mprtpr_path_set_chunks_reported(MpRTPRPath *this)
{
  THIS_WRITELOCK (this);
  this->owd_rle.read_index = this->owd_rle.write_index;
  THIS_WRITEUNLOCK (this);
}

void
mprtpr_path_set_discard_treshold(MpRTPRPath *this, GstClockTime treshold)
{
  THIS_WRITELOCK (this);
  this->discard_treshold = treshold;
  THIS_WRITEUNLOCK (this);
}

void
mprtpr_path_set_lost_treshold(MpRTPRPath *this, GstClockTime treshold)
{
  THIS_WRITELOCK (this);
  this->lost_treshold = treshold;
  THIS_WRITEUNLOCK (this);
}

void
mprtpr_path_set_owd_window_treshold(MpRTPRPath *this, GstClockTime treshold)
{
  THIS_WRITELOCK (this);
  percentiletracker_set_treshold(this->delays, treshold);
  THIS_WRITEUNLOCK (this);
}


GstRTCPXR_Chunk *
mprtpr_path_get_owd_chunks(MpRTPRPath *this,
                              guint *chunks_num,
                              guint16 *begin_seq,
                              guint16 *end_seq,
                              guint32 *offset)
{
  GstRTCPXR_Chunk *result = NULL, *chunk;
  gboolean chunk_type = FALSE;
  gboolean run_type;
  OWDRLEBlock *block;
  guint16 begin_seq_, end_seq_;
  gint i,chunks_num_;
  guint16 *read,running_length;
  GstClockTime abs_offset = -1;

  chunks_num_ = 0;
  THIS_READLOCK (this);
  for(i=this->owd_rle.read_index; ;){
    ++chunks_num_;
    if(i == this->owd_rle.write_index) break;
    if(++i == MPRTP_PLUGIN_MAX_RLE_LENGTH) i=0;
  }
  if((chunks_num_ % 2) == 1) ++chunks_num_;
  chunk = result = mprtp_malloc(sizeof(GstRTCPXR_Chunk) * chunks_num_);
  block = &this->owd_rle.blocks[this->owd_rle.read_index];
  begin_seq_ = block->start_seq;
  //get the offset
  for(i=this->owd_rle.read_index; ; )
  {
    {
      GstClockTime owd;
      owd = this->owd_rle.blocks[i].median_delay;
      if(owd < abs_offset){
        abs_offset = owd;
      }
    }
    if(i == this->owd_rle.write_index) break;
    if(++i == MPRTP_PLUGIN_MAX_RLE_LENGTH) i=0;
  }

  abs_offset -= abs_offset % GST_SECOND;
  *offset = abs_offset / GST_SECOND;

  for(i=this->owd_rle.read_index; ; )
  {
    end_seq_ = block->end_seq;
    run_type = i != this->owd_rle.write_index;
    {
      GstClockTime owd;
      owd = this->owd_rle.blocks[i].median_delay;
      owd -= abs_offset;
      //owd = GST_TIME_AS_MSECONDS(owd);
      //running_length = (owd > 0x3FFF) ? 0x3FFF : owd;
      if(owd < GST_SECOND){
        gdouble y = (gdouble) owd / (gdouble) GST_SECOND;
        y *= 16384;
        running_length = y;
      }else{
        running_length = 0x3FFF;
      }
      read = &running_length;
    }

    gst_rtcp_xr_chunk_change(chunk,
                             &chunk_type,
                             &run_type,
                             read);
    if(i == this->owd_rle.write_index) break;
    ++chunk;
    ++block;
    if(++i == MPRTP_PLUGIN_MAX_RLE_LENGTH) i=0;
  }
  THIS_READUNLOCK (this);
  if(chunks_num) *chunks_num = chunks_num_;
  if(begin_seq) *begin_seq = begin_seq_;
  if(end_seq) *end_seq = end_seq_;
  return result;
}


void mprtpr_path_get_joiner_stats(MpRTPRPath *this,
                           gdouble       *path_delay,
                           gdouble       *path_skew)
{
  THIS_READLOCK (this);
  if(path_delay) *path_delay = this->path_delay;
  if(path_skew)  *path_skew = this->path_skew; //this->estimated_skew;
  THIS_READUNLOCK (this);
}

void mprtpr_path_get_1s_rate_stats(MpRTPRPath *this,
                                   guint32 *expected_packetsrate,
                                   guint32 *receiver_byterate,
                                   guint32 *receiver_packetrate,
                                   guint32 *goodput_byterate,
                                   guint32 *goodput_packetrate)
{
  THIS_READLOCK (this);

  if(receiver_byterate) {
    *receiver_byterate = this->receiver_byterate;
  }

  if(receiver_packetrate){
    *receiver_packetrate = this->receiver_packetrate;
  }

  if(goodput_byterate){
    *goodput_byterate = this->goodput_byterate;
  }

  if(goodput_packetrate){
    *goodput_packetrate = this->goodput_packetrate;
  }
  THIS_READUNLOCK (this);
}

void mprtpr_path_tick(MpRTPRPath *this)
{
  THIS_WRITELOCK (this);
  _obsolate_discarded(this);
  _obsolate_misordered(this);
  THIS_WRITEUNLOCK (this);
}

void mprtpr_path_add_discard(MpRTPRPath *this, GstMpRTPBuffer *mprtp)
{
  THIS_WRITELOCK (this);
  _add_discard(this, mprtp->payload_bytes);
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
    _add_skew(this, skew);
  }

  //collect delays
  _add_delay(this, mprtp->delay);

  if(_cmp_seq(mprtp->subflow_seq, this->highest_seq) <= 0){
    _find_misordered_packet(this, mprtp->subflow_seq, mprtp->payload_bytes);
    goto done;
  }

  if(_cmp_seq(this->highest_seq + 1, mprtp->subflow_seq) < 0){
    guint16 seq = this->highest_seq + 1;
    for(; _cmp_seq(seq, mprtp->subflow_seq) < 0; ++seq){
      _add_packet_to_misordered(this, seq);
    }
  }else{

    //packet is in order
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
_cmp_seq (guint16 x, guint16 y)
{
  if(x == y) return 0;
  if(x < y && y - x < 32768) return -1;
  if(x > y && x - y > 32768) return -1;
  if(x < y && y - x > 32768) return 1;
  if(x > y && x - y < 32768) return 1;
  return 0;
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

void _add_discard(MpRTPRPath *this, guint payload_len)
{
  ++this->total_packets_discarded;
  this->total_payload_discarded+=payload_len;
}

void _add_delay(MpRTPRPath *this, GstClockTime delay)
{
  gint64 median_skew;
  percentiletracker_add(this->delays, delay);
  median_skew = percentiletracker_get_stats(this->delays, NULL, NULL, NULL);
  this->path_delay = this->path_delay * .99 + (gdouble) median_skew * .01;
}

void _add_skew(MpRTPRPath *this, gint64 skew)
{
  gint64 median_skew;
  percentiletracker2_add(this->skews, skew);
  median_skew = percentiletracker2_get_stats(this->skews, NULL, NULL, NULL);
  this->path_skew = this->path_skew * .99 + (gdouble) median_skew * .01;
}

void _obsolate_discarded(MpRTPRPath *this)
{
  MisorderedMPRTPPacket* item;
  GstClockTime now;

  now = _now(this);
  while(!g_queue_is_empty(this->discarded)){
    item = g_queue_peek_head(this->discarded);
    if(item->received){
      item = g_queue_pop_head(this->discarded);
      item->used = FALSE;
      continue;
    }
    if(item->added < now - this->lost_treshold){
      item = g_queue_pop_head(this->discarded);
      ++this->total_packets_lost;
      item->used = FALSE;
      continue;
    }
    break;
  }
}

void _add_packet_to_discarded(MpRTPRPath *this, guint16 seq, guint payload_len)
{
  MisorderedMPRTPPacket* item;

  _obsolate_discarded(this);

  item = &this->discarded_itemsbed[this->discarded_itemsbed_index];
  if(item->used){
    g_warning("Itemsbed for discards seems to be small");
    return;
  }
  this->discarded_itemsbed_index = (this->discarded_itemsbed_index + 1) % MPRTPR_DISCARDED_ITEMSBED_LENGTH;

  item->seq = seq;
  item->added = _now(this) - this->discard_treshold;
  item->received = FALSE;
  item->used = TRUE;
  item->payload_len = payload_len;

  g_queue_push_tail(this->discarded, item);
  ++this->total_packets_discarded_or_lost;
}

void _obsolate_misordered(MpRTPRPath *this)
{
  MisorderedMPRTPPacket* item;
  GstClockTime now;

  now = _now(this);
  while(!g_queue_is_empty(this->misordered)){
    item = g_queue_peek_head(this->misordered);
    if(item->received){
      item = g_queue_pop_head(this->misordered);
      item->used = FALSE;
      continue;
    }
    if(item->added < now - this->discard_treshold){
      item = g_queue_pop_head(this->misordered);
      _add_packet_to_discarded(this, item->seq, item->payload_len);
      item->used = FALSE;
      continue;
    }
    break;
  }
}

void _add_packet_to_misordered(MpRTPRPath *this, guint16 seq)
{
  MisorderedMPRTPPacket* item;

  _obsolate_misordered(this);

  item = &this->misordered_itemsbed[this->misordered_itemsbed_index];
  if(item->used){
    g_warning("Itemsbed for misordered seems to be small");
    return;
  }
  this->misordered_itemsbed_index = (this->misordered_itemsbed_index + 1) % MPRTPR_MISORDERED_ITEMSBED_LENGTH;

  item->seq = seq;
  item->added = _now(this);
  item->received = FALSE;
  item->used = TRUE;
  item->payload_len = 0;

  g_queue_push_tail(this->misordered, item);

}

static gint _find_packet_helper(gconstpointer ptr2item, gconstpointer ptr2searched_seq)
{
  const guint16 *seq;
  const MisorderedMPRTPPacket*item;
  seq = ptr2searched_seq;
  item = ptr2item;
  return item->seq == *seq ? 0 : -1;
}

void _find_misordered_packet(MpRTPRPath * this, guint16 seq, guint payload_len)
{
  GList *it;
  it = g_queue_find_custom(this->misordered, &seq, _find_packet_helper);
  if(it != NULL){
    ((MisorderedMPRTPPacket*) it->data)->received = TRUE;
    goto done;
  }

  it = g_queue_find_custom(this->discarded, &seq, _find_packet_helper);
  if(it != NULL){
    ((MisorderedMPRTPPacket*) it->data)->received = TRUE;
    _add_discard(this, payload_len);
    goto done;
  }

done:
  return;
}


#undef THIS_READLOCK
#undef THIS_READUNLOCK
#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
