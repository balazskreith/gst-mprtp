
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/rtp/gstrtpbuffer.h>
#include <gst/rtp/gstrtcpbuffer.h>
#include <stdio.h>
#include "mprtprpath.h"
#include "mprtpspath.h"
#include "gstmprtcpbuffer.h"

GST_DEBUG_CATEGORY_STATIC (gst_mprtpr_path_debug_category);
#define GST_CAT_DEFAULT gst_mprtpr_path_debug_category

G_DEFINE_TYPE (MpRTPRPath, mprtpr_path, G_TYPE_OBJECT);


#define THIS_READLOCK(this) g_rw_lock_reader_lock(&this->rwmutex)
#define THIS_READUNLOCK(this) g_rw_lock_reader_unlock(&this->rwmutex)
#define THIS_WRITELOCK(this) g_rw_lock_writer_lock(&this->rwmutex)
#define THIS_WRITEUNLOCK(this) g_rw_lock_writer_unlock(&this->rwmutex)

#define GET_SKEW_TREE_TOP(tree) (tree->top)
#define GET_SKEW_TREE_NUM(tree) (tree->counter)

typedef struct _Gap Gap;
typedef struct _Packet Packet;
typedef struct _Skew Skew;

struct _Gap
{
  Packet *at;
  guint16 start;
  guint16 end;
  guint16 total;
  guint16 filled;
};

struct _Skew
{
  Skew *left, *right;
  guint64 value;
  guint ref;
};

struct _SkewTree
{
  Skew *root;
  guint64 top;
    gint (*cmp) (guint64, guint64);
  gint counter;
  gchar name[255];
};

struct _SkewChain
{
  Packet *head, *tail, *play, *tree;
  gint counter;
};


struct _Packet
{
  GstBuffer *buffer;
  GstClockTime arrival;
  GstClockTimeDiff interleave;
  Packet *next;
  Packet *successor;
  Gap *gap;
  guint32 payload_bytes;
  guint16 abs_seq_num;
  guint16 rel_seq_num;
  GstClockTime rcv_time;
  GstClockTime snd_time;
  GstClockTimeDiff delay;
  guint64 skew;

};


static void mprtpr_path_finalize (GObject * object);
static void mprtpr_path_reset (MpRTPRPath * this);

static Packet *_make_packet (MpRTPRPath * this,
    GstRTPBuffer * rtp, guint16 packet_subflow_seq_num, GstClockTime snd_time);
static void _trash_packet (MpRTPRPath * this, Packet * packet);
static void _add_packet (MpRTPRPath * this, Packet * packet);
static void _chain_packets (MpRTPRPath * this, Packet * actual, Packet * next);
static void _ruin_skew_tree (MpRTPRPath * this, SkewTree * tree);
static void
_reset_skew_trees (MpRTPRPath * this, guint64 first, guint64 second);
static void _add_packet_skew (MpRTPRPath * this, guint64 skew);
static void _balancing_skew_tree (MpRTPRPath * this);
static void
_insert_skew_tree (MpRTPRPath * this, SkewTree * tree, guint64 value,
    guint ref_count);
static Skew *_insert_skew (MpRTPRPath * this, Skew * node, gint (*cmp) (guint64,
        guint64), guint64 value, guint ref_count);
static void _remove_skew (MpRTPRPath * this, guint64 skew);
static void
_remove_skew_tree (MpRTPRPath * this, SkewTree * tree, guint64 value,
    guint * ref_count);
static void
_remove_skew_value_from_tree (MpRTPRPath * this, SkewTree * tree,
    guint64 value, guint * ref_count);
static guint64 _get_new_top_skew (Skew * node);
static void
_make_gap (MpRTPRPath * this, Packet * at, guint16 start, guint16 end);
static void _trash_gap (MpRTPRPath * this, Gap * gap);
static Skew *_make_skew (MpRTPRPath * this, guint64 skew, guint ref_count);
static void _trash_skew (MpRTPRPath * this, Skew * skew);
static gboolean _try_fill_a_gap (MpRTPRPath * this, Packet * packet);
static gint _cmp_seq (guint16 x, guint16 y);
static gint _cmp_for_max (guint64 x, guint64 y);
static gint _cmp_for_min (guint64 x, guint64 y);
#define _print_tree(tree, node, level)
//
//void static
//_print_tree (SkewTree * tree, Skew* node, gint level)
//{
//  gint i;
//  if (node == NULL) {
//    return;
//  }
//  if (!level)
//    g_print ("Tree %p TOP is: %lu \n", tree, tree->top);
//  for (i = 0; i < level && i < 10; ++i)
//    g_print ("--");
//  g_print ("%d->%p->value:%lu ->ref: %u ->left:%p ->right:%p\n",
//      level, node, node->value, node->ref, node->left, node->right);
//  _print_tree (tree, node->left, level + 1);
//  _print_tree (tree, node->right, level + 1);
//}


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
  this->packet_chain = (PacketChain *) g_malloc0 (sizeof (PacketChain));
  this->packets_pool = g_queue_new ();
  this->skews_pool = gst_queue_array_new (1024);
  this->gaps_pool = g_queue_new ();
  this->skew_max_tree = g_malloc0 (sizeof (SkewTree));
  sprintf (this->skew_max_tree->name, "Max Tree");
  this->skew_min_tree = g_malloc0 (sizeof (SkewTree));
  sprintf (this->skew_min_tree->name, "Min Tree");
  this->skew_max_tree->cmp = _cmp_for_max;
  this->skew_min_tree->cmp = _cmp_for_min;
  this->last_median = GST_MSECOND;
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
  while (g_queue_is_empty (this->packets_pool))
    g_free (g_queue_pop_head (this->packets_pool));
  while (g_queue_is_empty (this->gaps_pool))
    g_free (g_queue_pop_head (this->gaps_pool));
  //  while (g_queue_is_empty (this->skews_pool))
  //    g_free (g_queue_pop_head (this->skews_pool));
  while (gst_queue_array_is_empty (this->skews_pool))
    g_free (gst_queue_array_pop_head (this->skews_pool));
  g_free (this->skew_max_tree);
  g_free (this->skew_min_tree);

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
  this->received_since_cycle_is_increased = 0;
  this->total_late_discarded = 0;
  this->total_late_discarded_bytes = 0;
  this->total_early_discarded = 0;
  this->total_duplicated_packet_num = 0;
  this->highest_seq = 0;
  this->total_packet_losts = 0;
  this->total_packet_received = 0;

  this->ext_rtptime = -1;
  this->last_packet_skew = 0;
//  this->skews_write_index = 0;
//  this->skews_read_index = 0;
  this->last_received_time = 0;
  this->played_highest_seq = 0;

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
  result = this->total_received_bytes;
  THIS_READUNLOCK (this);
  return result;
}

guint64
mprtpr_path_get_total_received_packets_num (MpRTPRPath * this)
{
  guint32 result;
  THIS_READLOCK (this);
  result = this->total_packet_received;
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


gboolean
mprtpr_path_has_buffer_to_playout (MpRTPRPath * this)
{
  gboolean result;
  THIS_READLOCK (this);
  result = this->packet_chain->play != NULL;
//  {
//    Packet *p;gint i = 0;
//    for(p = this->packet_chain->play; p; p = p->successor, ++i);
//    g_print("Waiting for playout: %d number of items in the chain: %d\n", i, this->packet_chain->counter);
//  }
  THIS_READUNLOCK (this);
  return result;
}

GstBuffer *
mprtpr_path_pop_buffer_to_playout (MpRTPRPath * this, guint16 * abs_seq)
{
  GstBuffer *result = NULL;
  PacketChain *chain;
  Packet *packet;
  THIS_WRITELOCK (this);
  chain = this->packet_chain;
  packet = chain->play;
  if (!packet)
    goto done;
  result = packet->buffer;
//  g_print("PLAYOUT: %p-%u<-%d:%u\n",
//          packet->buffer, packet->abs_seq_num,this->id,
//          this->highest_seq);
  packet->buffer = NULL;
  chain->play = packet->successor;
  if (packet->gap) {
    Gap *gap;
    gap = packet->gap;
    this->total_packet_losts += gap->total - gap->filled;
    this->gaps = g_list_remove (this->gaps, gap);
    packet->gap = NULL;
    _trash_gap (this, gap);
  }
  if(_cmp_seq(this->played_highest_seq, packet->rel_seq_num) < 0){
    this->played_highest_seq = packet->rel_seq_num;
  }
  if (abs_seq) {
    *abs_seq = packet->abs_seq_num;
  }
done:
//  g_print("packet abs seq: %p %hu packets in chain: %hu\n",
//          chain->play, packet->abs_seq_num, chain->counter);
  THIS_WRITEUNLOCK (this);
  return result;
}

void
mprtpr_path_removes_obsolate_packets (MpRTPRPath * this)
{
  Packet *actual, *next = NULL;
  PacketChain *chain;
  GstClockTime treshold;
  gint num = 0;
  THIS_WRITELOCK (this);
  treshold = gst_clock_get_time (this->sysclock) - 2 * GST_SECOND;
  chain = this->packet_chain;
  if (!chain->head)
    goto done;
  for (actual = chain->head; actual && actual->rcv_time < treshold;) {
    if (actual->skew)
      _remove_skew (this, actual->skew);
    next = actual->next;
    _trash_packet (this, actual);
    chain->head = actual = next;
    --chain->counter;
    ++num;
  }
//  g_print("Obsolate next: %p - %d packets\n", next, num);
  _balancing_skew_tree (this);
done:

  THIS_WRITEUNLOCK (this);
}

guint64
mprtpr_path_get_skew (MpRTPRPath * this)
{
  PacketChain *chain;
  guint64 result;
  SkewTree *min_tree, *max_tree;
  gint max_count, min_count;
  guint64 min_top, max_top;

  THIS_WRITELOCK (this);
  result = this->last_median;
  chain = this->packet_chain;
  if (chain->counter < 2)
    goto done;
  if (chain->counter == 2) {
    result = chain->head->next->skew;
    goto done;
  }
  min_tree = this->skew_min_tree;
  max_tree = this->skew_max_tree;
  min_count = GET_SKEW_TREE_NUM (min_tree);
  max_count = GET_SKEW_TREE_NUM (max_tree);
  min_top = GET_SKEW_TREE_TOP (min_tree);
  max_top = GET_SKEW_TREE_TOP (max_tree);

  if (min_count == max_count) {
//    g_print ("MnT: %lu MxT: %lu\n", min_top, max_top);
    result = (min_top + max_top) >> 1;
  } else if (min_count < max_count) {
//    g_print ("MxT: %lu\n", max_top);
    result = max_top;
  } else {
//    g_print ("MnT: %lu\n", min_top);
    result = min_top;
  }

done:
  this->last_median = result;
//  {
//    Packet *p, *mp = NULL;
//    gint i;
//    guint64 min = 0xffffffffffffffffUL, last = 0;
//    for(i=0; i<this->packet_chain->counter; ++i) {
//      for(p=this->packet_chain->head; p; p = p->successor)
//        min = p->skew < min && p->skew > last? mp = p, p->skew : min;
//      g_print("%lu-", mp->skew);
//      last = min;
//      min = 0xffffffffffffffffUL;
//    }
//    g_print("\n%p %d MEDIAN: %lu<-%d = %d + %d\n",
//            this, this->id, result, chain->counter,
//            this->skew_max_tree->counter,
//            this->skew_min_tree->counter);
//  }

  THIS_WRITEUNLOCK (this);
  return result;
}

void
mprtpr_path_process_rtp_packet (MpRTPRPath * this,
    GstRTPBuffer * rtp, guint16 packet_subflow_seq_num, GstClockTime snd_time)
{
  Packet *packet;

  THIS_WRITELOCK (this);

  packet = _make_packet (this, rtp, packet_subflow_seq_num, snd_time);

  if (this->seq_initialized == FALSE) {
    this->highest_seq = packet_subflow_seq_num;
    this->played_highest_seq = packet_subflow_seq_num;
    this->total_packet_received = 1;
    this->seq_initialized = TRUE;
    //this->result = g_list_prepend (this->result, buf);
    _add_packet (this, packet);
    goto done;
  }
  //goto mprtpr_path_process_rtpbuffer_end;

  //calculate lost, discarded and received packets
  ++this->total_packet_received;
  if (0x8000 < this->played_highest_seq && packet_subflow_seq_num < 0x8000 &&
      this->received_since_cycle_is_increased > 0x8888) {
    this->received_since_cycle_is_increased = 0;
    ++this->cycle_num;
  }
  _add_packet (this, packet);
  if (_cmp_seq (this->played_highest_seq, packet_subflow_seq_num) > 0) {
    ++this->total_late_discarded;
    this->total_late_discarded_bytes += packet->payload_bytes;
//    g_print("LATE DISCARDED: %p-%hu<-%d\n", packet, packet->abs_seq_num, this->id);
    goto delete_and_done;
  }



  this->total_received_bytes += packet->payload_bytes + (28 << 3);
  if (packet_subflow_seq_num == (guint16) (this->highest_seq + 1)) {
    ++this->received_since_cycle_is_increased;
    ++this->highest_seq;
    goto done;
  }
  if (_cmp_seq (this->highest_seq, packet_subflow_seq_num) < 0) {        //GAP
    _make_gap (this, packet, this->highest_seq, packet_subflow_seq_num);
    this->highest_seq = packet_subflow_seq_num;
    goto done;
  }
  if (_cmp_seq (this->highest_seq, packet_subflow_seq_num) > 0) {
//    g_print("TRY GAP FILLING: %p-%hu<-%d\n", packet, packet->abs_seq_num, this->id);
    if (_try_fill_a_gap (this, packet))
      goto done;
//    g_print("GAP FILLING WENT WRONG: %p-%hu<-%d\n", packet, packet->abs_seq_num, this->id);
  }
  ++this->total_duplicated_packet_num;

delete_and_done:
  gst_buffer_unref (packet->buffer);
  packet->buffer = NULL;
done:
  THIS_WRITEUNLOCK (this);
  return;

}

Packet *
_make_packet (MpRTPRPath * this,
    GstRTPBuffer * rtp, guint16 packet_subflow_seq_num, GstClockTime snd_time)
{
  Packet *result;
  guint32 rtptime;
  if (g_queue_is_empty (this->packets_pool)) {
    result = (Packet *) g_malloc0 (sizeof (Packet));
//    g_print("Created - ");
  } else {
    result = g_queue_pop_head (this->packets_pool);
//    g_print("Pooled - ");
  }

  rtptime = gst_rtp_buffer_get_timestamp (rtp);
  this->ext_rtptime =
      gst_rtp_buffer_ext_timestamp (&this->ext_rtptime, rtptime);

  result->buffer = rtp->buffer;
  gst_buffer_ref (result->buffer);
  result->gap = NULL;
  result->next = NULL;
  result->successor = NULL;
  result->payload_bytes = gst_rtp_buffer_get_payload_len (rtp);
  result->abs_seq_num = gst_rtp_buffer_get_seq (rtp);
  result->rel_seq_num = packet_subflow_seq_num;
  result->rcv_time = gst_clock_get_time (this->sysclock);
  result->snd_time = snd_time;
  result->skew = 0;
  result->delay = GST_CLOCK_DIFF (result->snd_time, result->rcv_time);
//  g_print("MAKED:%p-%hu<-%d\n", result->buffer, result->abs_seq_num,this->id);
//
//  g_print("Packet->buf: %p\n"
//          "Packet->payload_bytes: %u\n"
//          "Packet->abs_seq: %hu\n"
//          "Packet->rel_seq: %hu\n"
//          "Packet->rcv_time: %lu\n"
//          "Packet->snd_time: %lu\n", result->buffer, result->payload_bytes,
//          result->abs_seq_num, result->rel_seq_num, result->rcv_time,
//          result->snd_time);

  return result;
}

void
_trash_packet (MpRTPRPath * this, Packet * packet)
{
  if (g_queue_get_length (this->packets_pool) < 2048) {
    g_queue_push_tail (this->packets_pool, packet);
//    g_print("Recycle %d- ", g_queue_get_length (this->packets_pool));
  } else {
    g_free (packet);
//    g_print("Freed - ");
  }
}

void
_add_packet (MpRTPRPath * this, Packet * packet)
{
  PacketChain *chain;
  chain = this->packet_chain;
  if (!chain->head) {
    chain->head = chain->play = chain->tail = packet;
    chain->counter = 1;
    goto done;
  }
//g_print("CHAINED:%p-%hu<-%d\n",packet->buffer, packet->abs_seq_num,this->id);
  _chain_packets (this, chain->tail, packet);
  chain->tail = chain->tail->successor = packet;
  GST_DEBUG_OBJECT (this, "ADD PACKET CALLED WITH SKEW %lu\n", packet->skew);
  if (!chain->play)
    chain->play = packet;
  if (!chain->tree)
    chain->tree = packet;

  if (++chain->counter < 3) {
    GST_DEBUG_OBJECT (this, "CHAIN COUNTER SMALLER THAN 3\n");
    goto done;
  } else if (chain->counter == 3) {
    GST_DEBUG_OBJECT (this, "CHAIN COUNTER IS 3\n");
    _reset_skew_trees (this, chain->head->next->skew,
        chain->head->next->next->skew);
    goto done;
  }
  //calculate everything
  _add_packet_skew (this, packet->skew);
  if (GET_SKEW_TREE_NUM (this->skew_max_tree) +
      GET_SKEW_TREE_NUM (this->skew_min_tree) > 256) {
    Packet *actual;
    actual = chain->tree;
    if (actual->skew)
      _remove_skew (this, actual->skew);
    actual->skew = 0;
    chain->tree = actual->next;
  }

done:
  //++chain->counter;
  //g_print("Packets in the chain: %d\n", chain->counter);
  return;
}


void
_chain_packets (MpRTPRPath * this, Packet * actual, Packet * next)
{
  guint64 snd_diff, rcv_diff;
  //guint64 received;

  rcv_diff = next->rcv_time - actual->rcv_time;
  if (rcv_diff > 0x8000000000000000) {
    GST_WARNING_OBJECT (this, "The skew between two packets NOT real: "
            "prev snd: %lu act snd: %lu",actual->snd_time, next->snd_time);
    goto done;
  }

  if(actual->snd_time < next->snd_time)
    snd_diff = next->snd_time - actual->snd_time;
  else
    snd_diff = actual->snd_time - next->snd_time;

  if (rcv_diff < snd_diff)
    next->skew = snd_diff - rcv_diff;
  else
    next->skew = rcv_diff - snd_diff;

//  g_print("RCV_DIFF: %lu SND_DIFF: %lu PACKET SKEW: %lu\n",
//          rcv_diff, snd_diff, next->skew);
  if (next->skew > 0x8000000000000000) {
    GST_WARNING_OBJECT (this, "The skew between two packets NOT real");
    next->skew = 0;
    goto done;
  }

  this->jitter = this->jitter +
      (((gfloat) next->skew - (gfloat) this->jitter) / 16.);

done:
  actual->next = next;
  return;
}

void
_ruin_skew_tree (MpRTPRPath * this, SkewTree * tree)
{
  guint ref_count;
  while (tree->root) {
//      g_print("Tree top: %lu\n", tree->top);
    _remove_skew_tree (this, tree, tree->top, &ref_count);
  }
  tree->root = NULL;
  tree->counter = 0;
  tree->top = 0;
}



void
_reset_skew_trees (MpRTPRPath * this, guint64 first, guint64 second)
{
  //make sure there is nothing in the trees
  if (this->skew_max_tree)
    _ruin_skew_tree (this, this->skew_max_tree);
  if (this->skew_min_tree)
    _ruin_skew_tree (this, this->skew_min_tree);

  if (first < second) {
    _insert_skew_tree (this, this->skew_max_tree, first, 1);
    _insert_skew_tree (this, this->skew_min_tree, second, 1);
  } else {
    _insert_skew_tree (this, this->skew_min_tree, first, 1);
    _insert_skew_tree (this, this->skew_max_tree, second, 1);
  }
}

void
_add_packet_skew (MpRTPRPath * this, guint64 skew)
{
  GST_DEBUG_OBJECT (this,
      "ADD PACKET SKEW CALLED WITH MAX TREE TOP %lu, SKEW TO ADD IS %lu\n",
      GET_SKEW_TREE_TOP (this->skew_max_tree), skew);
  if (GET_SKEW_TREE_TOP (this->skew_max_tree) < skew)
    _insert_skew_tree (this, this->skew_min_tree, skew, 1);
  else
    _insert_skew_tree (this, this->skew_max_tree, skew, 1);

  _balancing_skew_tree (this);

}


void
_remove_skew (MpRTPRPath * this, guint64 skew)
{
  GST_DEBUG_OBJECT (this, "REMOVE SKEW CALLED WITH %lu MAX TREE TOP IS %lu\n",
      skew, GET_SKEW_TREE_TOP (this->skew_max_tree));
  if (GET_SKEW_TREE_TOP (this->skew_max_tree) < skew) {
    _remove_skew_tree (this, this->skew_min_tree, skew, NULL);
  } else {
    _remove_skew_tree (this, this->skew_max_tree, skew, NULL);
  }
//
//  g_print("MAX: %d MIN:%d pooled: %d\n",
//          this->skew_max_tree->counter, this->skew_min_tree->counter,
//          g_queue_get_length(this->skews_pool));

}

void
_balancing_skew_tree (MpRTPRPath * this)
{
  gint min_tree_num;
  gint max_tree_num;
  gint diff;
  guint64 top;
  guint ref_count;

balancing:
  min_tree_num = GET_SKEW_TREE_NUM (this->skew_min_tree);
  max_tree_num = GET_SKEW_TREE_NUM (this->skew_max_tree);
  diff = max_tree_num - min_tree_num;
//  g_print("max_tree_num: %d, min_tree_num: %d\n", max_tree_num, min_tree_num);
  if (-2 < diff && diff < 2) {
    goto done;
  }
  if (diff < -1) {
    top = this->skew_min_tree->top;
    _remove_skew_tree (this, this->skew_min_tree, top, &ref_count);
    _insert_skew_tree (this, this->skew_max_tree, top, ref_count);
  } else if (1 < diff) {
    top = this->skew_max_tree->top;
    _remove_skew_tree (this, this->skew_max_tree, top, &ref_count);
    _insert_skew_tree (this, this->skew_min_tree, top, ref_count);
  }
  goto balancing;

done:
  return;
}


Skew *
_insert_skew (MpRTPRPath * this,
    Skew * node, gint (*cmp) (guint64, guint64), guint64 value, guint ref_count)
{
  gint cmp_result;
  if (!node)
    return _make_skew (this, value, ref_count);
  cmp_result = cmp (node->value, value);
  if (!cmp_result) {
    GST_DEBUG_OBJECT (this, "DUPLICATED: %lu", value);
    return ++node->ref, node;
  } else if (cmp_result < 0)
    node->right = _insert_skew (this, node->right, cmp, value, ref_count);
  else
    node->left = _insert_skew (this, node->left, cmp, value, ref_count);
  return node;
}

void
_insert_skew_tree (MpRTPRPath * this, SkewTree * tree,
    guint64 value, guint ref_count)
{
  tree->root = _insert_skew (this, tree->root, tree->cmp, value, ref_count);
  if (++tree->counter == 1)
    tree->top = value;
  if (tree->cmp (tree->top, value) < 0)
    tree->top = value;

  GST_DEBUG_OBJECT (this, "INSERT %s CALLED FOR %lu, TOP is %lu\n",
      tree->name, value, tree->top);
//  _print_tree(tree, tree->root, 0);
}

static Skew *
_get_removal_candidate (Skew * node)
{
  Skew *result;
  for (result = node->right; result->left; result = result->left);
  return result;
}


void
_remove_skew_value_from_tree (MpRTPRPath * this, SkewTree * tree,
    guint64 value, guint * ref_count)
{
  Skew *parent = NULL, *node, *next = NULL, *candidate;
  gint cmp;
  guint64 replace_value;
  guint replace_ref;
  GST_DEBUG_OBJECT (this, "REMOVE SKEW_VALUE_FROM_TREE %s CALLED WITH %lu\n",
      tree->name, value);
  for (node = tree->root; node; parent = node, node = next) {
    cmp = tree->cmp (node->value, value);
    GST_DEBUG_OBJECT (this, "CMP: node->value: %lu - value: %lu => %d\n",
        node->value, value, cmp);
    if (!cmp)
      break;
    next = cmp < 0 ? node->right : node->left;
  }
  if (!node) {
    goto not_found;
  } else if (ref_count) {
    *ref_count = node->ref;
  } else if (node->ref > 1) {
    goto survive;
  }

  if (!node->left && !node->right) {
    candidate = NULL;
    goto remove;
  } else if (!node->left) {
    candidate = node->right;
    goto remove;
  } else if (!node->right) {
    candidate = node->left;
    goto remove;
  } else {
    candidate = _get_removal_candidate (node);
    goto replace;
  }

survive:
  GST_DEBUG_OBJECT (this, "%lu SURVIVE in %p\n", value, tree);
  --node->ref;
  return;
not_found:
  GST_DEBUG_OBJECT (this, "%lu NOT FOUND in %p\n", value, tree);
  return;
remove:
  GST_DEBUG_OBJECT (this, "ELEMENT FOUND TO REMOVE: %lu\n", value);
  if (!parent)
    tree->root = candidate;
  else if (parent->left == node)
    parent->left = candidate;
  else
    parent->right = candidate;
  _trash_skew (this, node);
  --tree->counter;
  return;
replace:
  GST_DEBUG_OBJECT (this, "ELEMENT FOUND TO REPLACE: %lu->%lu\n", value,
      candidate->value);
  _print_tree (tree, tree->root, 0);
  replace_value = candidate->value;
  _remove_skew_value_from_tree (this, tree, candidate->value, &replace_ref);
  node->value = replace_value;
  node->ref = replace_ref;
  return;

}

guint64
_get_new_top_skew (Skew * node)
{
  Skew *rightest;
  if (!node)
    return 0;
  for (rightest = node; rightest->right; rightest = rightest->right);
  return rightest->value;
}

void
_remove_skew_tree (MpRTPRPath * this,
    SkewTree * tree, guint64 value, guint * ref_count)
{
  GST_DEBUG_OBJECT (this, "REMOVE SKEW FROM %s CALLED WITH %lu\n", tree->name,
      value);
  _remove_skew_value_from_tree (this, tree, value, ref_count);
  if (tree->top == value)
    tree->top = _get_new_top_skew (tree->root);
}


void
_make_gap (MpRTPRPath * this, Packet * at, guint16 start, guint16 end)
{
  Gap *gap;
  guint16 counter;
  if (g_queue_is_empty (this->gaps_pool))
    gap = g_malloc0 (sizeof (Gap));
  else
    gap = (Gap *) g_queue_pop_head (this->gaps_pool);

  gap->at = at;
  gap->start = start;
  gap->end = end;
  gap->filled = 0;
  gap->total = 1;
  for (counter = gap->start + 1;
      counter != (guint16) (gap->end - 1); ++counter, ++gap->total);
  this->gaps = g_list_prepend (this->gaps, gap);
  at->gap = gap;

}

void
_trash_gap (MpRTPRPath * this, Gap * gap)
{
  if (g_queue_get_length (this->gaps_pool) > 100)
    g_free (gap);
  else
    g_queue_push_head (this->gaps_pool, gap);
}


Skew *
_make_skew (MpRTPRPath * this, guint64 skew, guint ref_count)
{
  Skew *result;
  //if (g_queue_is_empty (this->skews_pool)){
  if (gst_queue_array_is_empty (this->skews_pool)) {
    result = g_malloc0 (sizeof (Skew));
//    g_print("Created - ");
  } else {
    result = (Skew *) gst_queue_array_pop_head (this->skews_pool);
//      g_print("Pooled - ");
    //result = (Skew *) g_queue_pop_head (this->skews_pool);
//    g_print("pooled: %d\n", g_queue_get_length(this->skews_pool));
  }
  result->right = result->left = NULL;
  result->value = skew;
  result->ref = ref_count;
  return result;
}

void
_trash_skew (MpRTPRPath * this, Skew * skew)
{
  if (gst_queue_array_get_length (this->skews_pool) > 1024) {
//      g_print("Freed - ");
    g_free (skew);
  } else {
    gst_queue_array_push_tail (this->skews_pool, skew);
    GST_DEBUG_OBJECT (this, "Trashed:%lu\n", skew->value);
  }
}


gboolean
_try_fill_a_gap (MpRTPRPath * this, Packet * packet)
{
  gboolean result;
  GList *it;
  Gap *gap;
  gint cmp;
  Packet *pred;
  Packet *succ;

  result = FALSE;
  for (it = this->gaps; it; it = it->next, gap = NULL) {
    gap = it->data;
    cmp = _cmp_seq (gap->start, packet->rel_seq_num);
    if (cmp > 0)
      continue;
    if (cmp == 0)
      goto done;

    cmp = _cmp_seq (packet->rel_seq_num, gap->end);
    if (cmp > 0)
      continue;
    if (cmp == 0)
      goto done;
    break;
  }

  if (!gap)
    goto done;

  for (pred = gap->at; pred->rel_seq_num != gap->end; pred = pred->successor) {
    succ = pred->successor;
    cmp = _cmp_seq (packet->rel_seq_num, succ->rel_seq_num);
    if (cmp > 0)
      break;
    succ = NULL;
  }

  if (!succ)
    goto done;

  pred->successor = packet;
  packet->successor = succ;
  result = TRUE;

done:
  return result;
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

#undef GET_SKEW_TREE_TOP
#undef GET_SKEW_TREE_NUM

#undef THIS_READLOCK
#undef THIS_READUNLOCK
#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
