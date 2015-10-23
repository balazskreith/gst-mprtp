/*
 * GStreamer
 *
 *  Copyright 2006 Collabora Ltd,
 *  Copyright 2006 Nokia Corporation
 *   @author: Philippe Kalaf <philippe.kalaf@collabora.co.uk>.
 *  Copyright 2012-2015 Pexip
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include "gstnetsimb.h"
#include "netsimqueue.h"

GST_DEBUG_CATEGORY (netsim_debug);
#define GST_CAT_DEFAULT (netsim_debug)

#define SENT_ARRAY_LENGTH 1024

enum
{
  ARG_0,
  ARG_MIN_DELAY,
  ARG_QUEUE_MIN_DELAY,
  ARG_QUEUE_MAX_DELAY,
  ARG_QUEUE_DROP_POLICY,
  ARG_QUEUE_MAX_PACKETS,
  ARG_MAX_DELAY,
  ARG_PACING_TRESHOLD,
  ARG_DELAY_PROBABILITY,
  ARG_REORDER_PROBABILITY,
  ARG_DROP_PROBABILITY,
  ARG_DUPLICATE_PROBABILITY,
  ARG_DROP_PACKETS
};

struct _GstNetSimbPrivate
{
  GstPad *sinkpad, *srcpad;

  GMutex loop_mutex;
  GCond start_cond;
  GMainLoop *main_loop;
  gboolean running;

  GRand *rand_seed;
  gint min_delay;
  gint max_delay;

  gboolean pop_task;

  gfloat delay_probability;
  gfloat reorder_probability;
  gfloat drop_probability;
  gfloat duplicate_probability;
  guint drop_packets;

  GstBuffer *has_sg_to_be_reordered;

  gint             pacing_trashold_in_kbs;
  GstClockTime     sent_times[SENT_ARRAY_LENGTH];
  guint8           sent_octets[SENT_ARRAY_LENGTH];
  guint16          sent_octets_read_index;
  guint16          sent_octets_write_index;
  gsize            actual_bytes;
  GQueue*          buffer_queue;
  GstClock*        sysclock;

  NetsimQueue*     queue;

  gint             queue_min_delay;
  gint             queue_max_delay;
  gint             queue_max_packets;
  gint             queue_drop_policy;
};


/* these numbers are nothing but wild guesses and dont reflect any reality */
#define DEFAULT_QUEUE_MIN_DELAY 0
#define DEFAULT_QUEUE_MAX_DELAY 0
#define DEFAULT_MIN_DELAY 200
#define DEFAULT_MAX_DELAY 400
#define DEFAULT_DELAY_PROBABILITY 0.0
#define DEFAULT_DROP_PROBABILITY 0.0
#define DEFAULT_DUPLICATE_PROBABILITY 0.0
#define DEFAULT_DROP_PACKETS 0

#define GST_NET_SIMB_GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), GST_TYPE_NET_SIMB, \
                                GstNetSimbPrivate))

static GstStaticPadTemplate gst_net_simb_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate gst_net_simb_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

G_DEFINE_TYPE (GstNetSimb, gst_net_simb, GST_TYPE_ELEMENT);

static void
gst_net_simb_loop (GstNetSimb * netsim)
{
  GMainLoop *loop;

  GST_TRACE_OBJECT (netsim, "TASK: begin");

  g_mutex_lock (&netsim->priv->loop_mutex);
  loop = g_main_loop_ref (netsim->priv->main_loop);
  netsim->priv->running = TRUE;
  GST_TRACE_OBJECT (netsim, "TASK: signal start");
  g_cond_signal (&netsim->priv->start_cond);
  g_mutex_unlock (&netsim->priv->loop_mutex);

  GST_TRACE_OBJECT (netsim, "TASK: run");
  g_main_loop_run (loop);
  g_main_loop_unref (loop);

  g_mutex_lock (&netsim->priv->loop_mutex);
  GST_TRACE_OBJECT (netsim, "TASK: pause");
  gst_pad_pause_task (netsim->priv->srcpad);
  netsim->priv->running = FALSE;
  GST_TRACE_OBJECT (netsim, "TASK: signal end");
  g_cond_signal (&netsim->priv->start_cond);
  g_mutex_unlock (&netsim->priv->loop_mutex);
  GST_TRACE_OBJECT (netsim, "TASK: end");
}

static gboolean
_main_loop_quit_and_remove_source (gpointer user_data)
{
  GMainLoop *main_loop = user_data;
  GST_DEBUG ("MAINLOOP: Quit %p", main_loop);
  g_main_loop_quit (main_loop);
  g_assert (!g_main_loop_is_running (main_loop));
  return FALSE;                 /* Remove source */
}

static gboolean
gst_net_simb_src_activatemode (GstPad * pad, GstObject * parent,
    GstPadMode mode, gboolean active)
{
  GstNetSimb *netsim = GST_NET_SIMB (parent);
  gboolean result = FALSE;

  (void) pad;
  (void) mode;

  g_mutex_lock (&netsim->priv->loop_mutex);
  if (active) {
    if (netsim->priv->main_loop == NULL) {
      GMainContext *main_context = g_main_context_new ();
      netsim->priv->main_loop = g_main_loop_new (main_context, FALSE);
      g_main_context_unref (main_context);

      GST_TRACE_OBJECT (netsim, "ACT: Starting task on srcpad");
      result = gst_pad_start_task (netsim->priv->srcpad,
          (GstTaskFunction) gst_net_simb_loop, netsim, NULL);

      GST_TRACE_OBJECT (netsim, "ACT: Wait for task to start");
      g_assert (!netsim->priv->running);
      while (!netsim->priv->running)
        g_cond_wait (&netsim->priv->start_cond, &netsim->priv->loop_mutex);
      GST_TRACE_OBJECT (netsim, "ACT: Task on srcpad started");
    }
  } else {
    if (netsim->priv->main_loop != NULL) {
      GSource *source;
      guint id;

      /* Adds an Idle Source which quits the main loop from within.
       * This removes the possibility for run/quit race conditions. */
      GST_TRACE_OBJECT (netsim, "DEACT: Stopping main loop on deactivate");
      source = g_idle_source_new ();
      g_source_set_callback (source, _main_loop_quit_and_remove_source,
          g_main_loop_ref (netsim->priv->main_loop),
          (GDestroyNotify) g_main_loop_unref);
      id = g_source_attach (source,
          g_main_loop_get_context (netsim->priv->main_loop));
      g_source_unref (source);
      g_assert_cmpuint (id, >, 0);
      g_main_loop_unref (netsim->priv->main_loop);
      netsim->priv->main_loop = NULL;

      GST_TRACE_OBJECT (netsim, "DEACT: Wait for mainloop and task to pause");
      g_assert (netsim->priv->running);
      while (netsim->priv->running)
        g_cond_wait (&netsim->priv->start_cond, &netsim->priv->loop_mutex);

      GST_TRACE_OBJECT (netsim, "DEACT: Stopping task on srcpad");
      result = gst_pad_stop_task (netsim->priv->srcpad);
      GST_TRACE_OBJECT (netsim, "DEACT: Mainloop and GstTask stopped");
    }
  }
  g_mutex_unlock (&netsim->priv->loop_mutex);

  return result;
}

typedef struct
{
  GstPad *pad;
  GstBuffer *buf;
  GstNetSimb *netsim;
} PushBufferCtx;

G_INLINE_FUNC PushBufferCtx *
push_buffer_ctx_new (GstPad * pad, GstBuffer * buf, GstNetSimb *netsim)
{
  PushBufferCtx *ctx = g_slice_new (PushBufferCtx);
  ctx->pad = gst_object_ref (pad);
  ctx->buf = gst_buffer_ref (buf);
  ctx->netsim = netsim;
  return ctx;
}

G_INLINE_FUNC void
push_buffer_ctx_free (PushBufferCtx * ctx)
{
  if (G_LIKELY (ctx != NULL)) {
    gst_buffer_unref (ctx->buf);
    gst_object_unref (ctx->pad);
    g_slice_free (PushBufferCtx, ctx);
  }
}

static gboolean
push_buffer_ctx_push (PushBufferCtx * ctx)
{
  GST_DEBUG_OBJECT (ctx->pad, "Pushing buffer now");
  //gst_pad_push (ctx->pad, gst_buffer_ref (ctx->buf));
  netsimqueue_push_buffer(ctx->netsim->priv->queue, ctx->buf);
  return FALSE;
}

static GstFlowReturn
gst_net_simb_delay_buffer (GstNetSimb * netsim, GstBuffer * buf)
{
  GstFlowReturn ret = GST_FLOW_OK;
  g_mutex_lock (&netsim->priv->loop_mutex);
  if (netsim->priv->main_loop != NULL && netsim->priv->delay_probability > 0 &&
      g_rand_double (netsim->priv->rand_seed) < netsim->priv->delay_probability)
  {
    PushBufferCtx *ctx = push_buffer_ctx_new (netsim->priv->srcpad, buf, netsim);
    gint delay = g_rand_int_range (netsim->priv->rand_seed,
        netsim->priv->min_delay, netsim->priv->max_delay);
    GSource *source = g_timeout_source_new (delay);

    GST_DEBUG_OBJECT (netsim, "Delaying packet by %d", delay);
    g_source_set_callback (source, (GSourceFunc) push_buffer_ctx_push,
        ctx, (GDestroyNotify) push_buffer_ctx_free);
    g_source_attach (source, g_main_loop_get_context (netsim->priv->main_loop));
    g_source_unref (source);
  } else {
    netsimqueue_push_buffer(netsim->priv->queue, buf);
    //ret = gst_pad_push (netsim->priv->srcpad, gst_buffer_ref (buf));
  }

  g_mutex_unlock (&netsim->priv->loop_mutex);

  return ret;
}

static gsize _get_packet_size(GstBuffer *buffer)
{
  gsize result;
  GstMapInfo map = GST_MAP_INFO_INIT;
  gst_buffer_map(buffer, &map, GST_MAP_READ);
  result = map.size;
  gst_buffer_unmap(buffer, &map);
  return result;
}

static void _add_packet_to_sent_octets(GstNetSimbPrivate *this, GstBuffer *buffer)
{
  gsize bytes;
  if(++this->sent_octets_write_index == SENT_ARRAY_LENGTH){
      this->sent_octets_write_index = 0;
  }
  bytes = _get_packet_size(buffer);
  this->sent_octets[this->sent_octets_write_index] = (guint8) (bytes>>3);
  this->sent_times[this->sent_octets_write_index] = gst_clock_get_time(this->sysclock);
  this->actual_bytes+= bytes;
}

static void _pop_from_sent_octets(GstNetSimbPrivate *this)
{
  gsize bytes;
  if(this->sent_octets_read_index == this->sent_octets_write_index){
      goto done;
  }
  if(++this->sent_octets_read_index == SENT_ARRAY_LENGTH){
      this->sent_octets_read_index = 0;
  }
  bytes = (gsize)this->sent_octets[this->sent_octets_read_index] << 3;
  this->actual_bytes-= bytes;
done:
  return;
}

static void _refresh_actual_bytes(GstNetSimbPrivate *this)
{
  GstClockTime sent,now;
  now = gst_clock_get_time(this->sysclock);
again:
  if(this->sent_octets_read_index == this->sent_octets_write_index){
      goto done;
  }
  sent = this->sent_times[this->sent_octets_read_index];
  if(sent < now - GST_SECOND){
    _pop_from_sent_octets(this);
    goto again;
  }
done:
  return;
}

static GstFlowReturn
gst_net_simb_chain (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
  GstNetSimb *netsim = GST_NET_SIMB (parent);
  GstFlowReturn ret = GST_FLOW_OK;

  (void) pad;

  if(netsim->priv->has_sg_to_be_reordered){
     gst_net_simb_delay_buffer (netsim, netsim->priv->has_sg_to_be_reordered);
     netsim->priv->has_sg_to_be_reordered = NULL;
     ret = gst_net_simb_delay_buffer (netsim, buf);
  }else if (netsim->priv->drop_packets > 0) {
    netsim->priv->drop_packets--;
    GST_DEBUG_OBJECT (netsim, "Dropping packet (%d left)",
        netsim->priv->drop_packets);
  } else if (netsim->priv->drop_probability > 0
      && g_rand_double (netsim->priv->rand_seed) <
      (gdouble) netsim->priv->drop_probability) {
    GST_DEBUG_OBJECT (netsim, "Dropping packet");
  } else if (netsim->priv->reorder_probability > 0
      && g_rand_double (netsim->priv->rand_seed) <
      (gdouble) netsim->priv->reorder_probability) {
    GST_DEBUG_OBJECT (netsim, "Reordering packet");
    netsim->priv->has_sg_to_be_reordered = buf;
  } else if (netsim->priv->duplicate_probability > 0 &&
      g_rand_double (netsim->priv->rand_seed) <
      (gdouble) netsim->priv->duplicate_probability) {
    GST_DEBUG_OBJECT (netsim, "Duplicating packet");
    gst_net_simb_delay_buffer (netsim, buf);
    ret = gst_net_simb_delay_buffer (netsim, buf);
  } else {
    ret = gst_net_simb_delay_buffer (netsim, buf);
  }

  gst_buffer_unref (buf);
  return ret;
}


static void
gst_net_simb_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstNetSimb *netsim = GST_NET_SIMB (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    case ARG_QUEUE_MIN_DELAY:
        netsim->priv->queue_min_delay = g_value_get_int (value);
        netsimqueue_set_min_time(netsim->priv->queue, netsim->priv->queue_min_delay);
        break;
    case ARG_QUEUE_MAX_DELAY:
        netsim->priv->queue_max_delay = g_value_get_int (value);
        netsimqueue_set_max_time(netsim->priv->queue, netsim->priv->queue_max_delay);
        break;
    case ARG_QUEUE_MAX_PACKETS:
      netsim->priv->queue_max_packets = g_value_get_int (value);
      netsimqueue_set_max_packets(netsim->priv->queue, netsim->priv->queue_max_packets);
      break;
    case ARG_QUEUE_DROP_POLICY:
      {
        NetsimQueueDropPolicy policy;
        netsim->priv->queue_drop_policy = g_value_get_int (value);
        if(netsim->priv->queue_drop_policy == 0)
          policy = NETSIMQUEUE_DROP_POLICY_MILK;
        else
          policy = NETSIMQUEUE_DROP_POLICY_WINE;
        netsimqueue_set_drop_policy(netsim->priv->queue, policy);
      }
        break;
    case ARG_MIN_DELAY:
        netsim->priv->min_delay = g_value_get_int (value);
        break;
    case ARG_MAX_DELAY:
      netsim->priv->max_delay = g_value_get_int (value);
      break;
    case ARG_PACING_TRESHOLD:
      netsim->priv->pacing_trashold_in_kbs = g_value_get_int (value);
      break;
    case ARG_DELAY_PROBABILITY:
      netsim->priv->delay_probability = g_value_get_float (value);
      break;
    case ARG_REORDER_PROBABILITY:
      netsim->priv->reorder_probability = g_value_get_float (value);
      break;
    case ARG_DROP_PROBABILITY:
      netsim->priv->drop_probability = g_value_get_float (value);
      break;
    case ARG_DUPLICATE_PROBABILITY:
      netsim->priv->duplicate_probability = g_value_get_float (value);
      break;
    case ARG_DROP_PACKETS:
      netsim->priv->drop_packets = g_value_get_uint (value);
      break;
  }
}

static void
gst_net_simb_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstNetSimb *netsim = GST_NET_SIMB (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    case ARG_QUEUE_MIN_DELAY:
      g_value_set_int (value, netsim->priv->queue_min_delay);
      break;
    case ARG_QUEUE_MAX_DELAY:
      g_value_set_int (value, netsim->priv->queue_max_delay);
      break;
    case ARG_QUEUE_DROP_POLICY:
      g_value_set_int (value, netsim->priv->queue_drop_policy);
      break;
    case ARG_QUEUE_MAX_PACKETS:
      g_value_set_int (value, netsim->priv->queue_max_packets);
      break;
    case ARG_MIN_DELAY:
      g_value_set_int (value, netsim->priv->min_delay);
      break;
    case ARG_MAX_DELAY:
      g_value_set_int (value, netsim->priv->max_delay);
      break;
    case ARG_PACING_TRESHOLD:
      g_value_set_int (value, netsim->priv->pacing_trashold_in_kbs);
      break;
    case ARG_DELAY_PROBABILITY:
      g_value_set_float (value, netsim->priv->delay_probability);
      break;
    case ARG_REORDER_PROBABILITY:
      g_value_set_float (value, netsim->priv->reorder_probability);
      break;
    case ARG_DROP_PROBABILITY:
      g_value_set_float (value, netsim->priv->drop_probability);
      break;
    case ARG_DUPLICATE_PROBABILITY:
      g_value_set_float (value, netsim->priv->duplicate_probability);
      break;
    case ARG_DROP_PACKETS:
      g_value_set_uint (value, netsim->priv->drop_packets);
      break;
  }
}

static gboolean
_popper (gpointer data) //pop data from queue
{
  GstNetSimb* this;
  GstBuffer *buf;

  this = data;
again:
  if(!this->priv->pacing_trashold_in_kbs)
    goto pop;
  _refresh_actual_bytes(this->priv);
  if(this->priv->pacing_trashold_in_kbs<<10 < this->priv->actual_bytes)
    goto done;
pop:
  buf = netsimqueue_pop_buffer(this->priv->queue);
  if(!buf) goto done;
  _add_packet_to_sent_octets(this->priv, buf);
  gst_pad_push(this->priv->srcpad, gst_buffer_ref(buf));
  goto again;
done:
  return this->priv->pop_task ? G_SOURCE_CONTINUE : G_SOURCE_REMOVE;
}

static void
gst_net_simb_init (GstNetSimb * netsim)
{
  netsim->priv = GST_NET_SIMB_GET_PRIVATE (netsim);

  netsim->priv->srcpad =
      gst_pad_new_from_static_template (&gst_net_simb_src_template, "src");
  netsim->priv->sinkpad =
      gst_pad_new_from_static_template (&gst_net_simb_sink_template, "sink");

  gst_element_add_pad (GST_ELEMENT (netsim), netsim->priv->srcpad);
  gst_element_add_pad (GST_ELEMENT (netsim), netsim->priv->sinkpad);

  g_mutex_init (&netsim->priv->loop_mutex);
  g_cond_init (&netsim->priv->start_cond);
  netsim->priv->rand_seed = g_rand_new ();
  netsim->priv->main_loop = NULL;
  netsim->priv->buffer_queue = g_queue_new();
  netsim->priv->sysclock = gst_system_clock_obtain();
  GST_OBJECT_FLAG_SET (netsim->priv->sinkpad,
      GST_PAD_FLAG_PROXY_CAPS | GST_PAD_FLAG_PROXY_ALLOCATION);

  gst_pad_set_chain_function (netsim->priv->sinkpad,
      GST_DEBUG_FUNCPTR (gst_net_simb_chain));
  gst_pad_set_activatemode_function (netsim->priv->srcpad,
      GST_DEBUG_FUNCPTR (gst_net_simb_src_activatemode));

  netsim->priv->queue = g_object_new(NETSIMQUEUE_TYPE, NULL);
  netsim->priv->pop_task = TRUE;
  g_timeout_add (1, _popper, netsim);
}

static void
gst_net_simb_finalize (GObject * object)
{
  GstNetSimb *netsim = GST_NET_SIMB (object);

  g_rand_free (netsim->priv->rand_seed);
  g_mutex_clear (&netsim->priv->loop_mutex);
  g_cond_clear (&netsim->priv->start_cond);
  g_object_unref(netsim->priv->sysclock);
  g_object_unref(netsim->priv->queue);
  G_OBJECT_CLASS (gst_net_simb_parent_class)->finalize (object);
}

static void
gst_net_simb_dispose (GObject * object)
{
  GstNetSimb *netsim = GST_NET_SIMB (object);

  g_assert (netsim->priv->main_loop == NULL);

  G_OBJECT_CLASS (gst_net_simb_parent_class)->dispose (object);
}

static void
gst_net_simb_class_init (GstNetSimbClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GstNetSimbPrivate));

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_net_simb_src_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_net_simb_sink_template));

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
      "Netsim", "Generic",
      "Netsim long name", "Balázs Kreith <balazs.kreith@gmail.com>"
                          "Philippe Kalaf <philippe.kalaf@collabora.co.uk>");


//  gst_element_class_set_details_simple (gstelement_class,
//      "Network Simulator",
//      "Filter/Network",
//      "An element that simulates network jitter, "
//      "packet loss and packet duplication",
//      "Philippe Kalaf <philippe.kalaf@collabora.co.uk>");

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_net_simb_dispose);
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_net_simb_finalize);

  gobject_class->set_property = gst_net_simb_set_property;
  gobject_class->get_property = gst_net_simb_get_property;

  g_object_class_install_property (gobject_class, ARG_QUEUE_MIN_DELAY,
          g_param_spec_int ("queue-min-delay", "minimal delay for packets in (ms)",
              "Constant delay applied for every buffers going through the elements",
              G_MININT, G_MAXINT, DEFAULT_QUEUE_MIN_DELAY,
              G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, ARG_QUEUE_MAX_DELAY,
          g_param_spec_int ("queue-max-delay", "maximal delay for packets in (ms)",
              "Packets will be dropped if they stay longer than this period",
              G_MININT, G_MAXINT, DEFAULT_QUEUE_MAX_DELAY,
              G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, ARG_QUEUE_MAX_DELAY,
          g_param_spec_int ("queue-drop-policy", "Policy for drop packets if the queue is full",
              "0 - milk policy, 1 - wine policy",
              0, 1, 0,
              G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, ARG_QUEUE_MAX_PACKETS,
          g_param_spec_int ("queue-max-packets", "Number of packets a queue can handle",
              "Number of packets the queue can handle",
              0, MAX_NETSIMQUEUEBUFFERS_NUM, MAX_NETSIMQUEUEBUFFERS_NUM,
              G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, ARG_MIN_DELAY,
          g_param_spec_int ("min-delay", "Minimum delay (ms)",
              "The minimum delay in ms to apply to buffers",
              G_MININT, G_MAXINT, DEFAULT_MIN_DELAY,
              G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, ARG_MAX_DELAY,
       g_param_spec_int ("max-delay", "Maximum delay (ms)",
           "The maximum delay in ms to apply to buffers",
           G_MININT, G_MAXINT, DEFAULT_MAX_DELAY,
           G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, ARG_PACING_TRESHOLD,
       g_param_spec_int ("pacing-treshold", "Maximum allowed bytes per 1s",
           "The maximum allowed bytes through the element per 1s in kbytes. 0 means infinity from the element side",
           G_MININT, G_MAXINT, 0,
           G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, ARG_DELAY_PROBABILITY,
       g_param_spec_float ("delay-probability", "Delay Probability",
           "The Probability a buffer is delayed",
           0.0, 1.0, DEFAULT_DELAY_PROBABILITY,
           G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, ARG_REORDER_PROBABILITY,
         g_param_spec_float ("reorder-probability", "Reorder Probability",
             "The Probability a buffer is reordered",
             0.0, 1.0, DEFAULT_DELAY_PROBABILITY,
             G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));


  g_object_class_install_property (gobject_class, ARG_DROP_PROBABILITY,
      g_param_spec_float ("drop-probability", "Drop Probability",
          "The Probability a buffer is dropped",
          0.0, 1.0, DEFAULT_DROP_PROBABILITY,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, ARG_DUPLICATE_PROBABILITY,
      g_param_spec_float ("duplicate-probability", "Duplicate Probability",
          "The Probability a buffer is duplicated",
          0.0, 1.0, DEFAULT_DUPLICATE_PROBABILITY,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, ARG_DROP_PACKETS,
      g_param_spec_uint ("drop-packets", "Drop Packets",
          "Drop the next n packets",
          0, G_MAXUINT, DEFAULT_DROP_PACKETS,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  GST_DEBUG_CATEGORY_INIT (netsim_debug, "netsim", 0, "Network simulator");


}
