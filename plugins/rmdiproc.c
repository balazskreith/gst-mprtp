/* GStreamer Scheduling tree
 * Copyright (C) 2015 Balázs Kreith (contact: balazs.kreith@gmail.com)
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/rtp/gstrtpbuffer.h>
#include <gst/rtp/gstrtcpbuffer.h>
#include "rmdiproc.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

//#define THIS_READLOCK(this) g_rw_lock_reader_lock(&this->rwmutex)
//#define THIS_READUNLOCK(this) g_rw_lock_reader_unlock(&this->rwmutex)
//#define THIS_WRITELOCK(this) g_rw_lock_writer_lock(&this->rwmutex)
//#define THIS_WRITEUNLOCK(this) g_rw_lock_writer_unlock(&this->rwmutex)

#define THIS_READLOCK(this)
#define THIS_READUNLOCK(this)
#define THIS_WRITELOCK(this)
#define THIS_WRITEUNLOCK(this)


GST_DEBUG_CATEGORY_STATIC (rmdi_processor_debug_category);
#define GST_CAT_DEFAULT cormdi_processor_debug_category

G_DEFINE_TYPE (RMDIProcessor, rmdi_processor, G_TYPE_OBJECT);

typedef struct _CorrBlock CorrBlock;

struct _CorrBlock{
  guint           id,N;
  gint64          Iu0,Iu1,Id1,Id2,Id3,G01,M0,M1,G_[64],M_[64];
  gint            index;
  gdouble         g;
  CorrBlock*     next;
};

typedef struct _RMDIProcessorPrivate{
  CorrBlock           cblocks[8];
  guint32             cblocks_counter;
  GstClockTime        delay80th;
  GstClockTime        min_delay;
}RMDIProcessorPrivate;


#define _priv(this) ((RMDIProcessorPrivate*) this->priv)
#define _now(this) (gst_clock_get_time(this->sysclock))
//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------
static void rmdi_processor_finalize (GObject * object);

static void _execute_corrblocks(RMDIProcessorPrivate *this, CorrBlock *blocks);
static void _execute_corrblock(CorrBlock* this);
static void _csv_logging(RMDIProcessor *this, gint64 delay);
static void _readable_logging(RMDIProcessor *this);
static void _readable_result(RMDIProcessor *this, RMDIProcessorResult *result);
static gint _cmp_seq (guint16 x, guint16 y);
void
rmdi_processor_class_init (RMDIProcessorClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = rmdi_processor_finalize;

  GST_DEBUG_CATEGORY_INIT (rmdi_processor_debug_category, "rmdi_processor", 0,
      "RMDIProcessor");

}

void
rmdi_processor_finalize (GObject * object)
{
  RMDIProcessor *this;
  this = RMDIPROCESSOR(object);

  mprtp_free(this->priv);
  g_object_unref(this->sysclock);
  this->packetstracker = mprtps_path_unref_packetstracker(this->path);
  g_object_unref(this->path);
}

void
rmdi_processor_init (RMDIProcessor * this)
{
  g_rw_lock_init (&this->rwmutex);
  this->sysclock = gst_system_clock_obtain();
  this->priv = g_malloc0(sizeof(RMDIProcessorPrivate));
}

static void _delay80th_pipe(gpointer data, PercentileTrackerPipeData *stats)
{
  RMDIProcessor *this = data;
  _priv(this)->delay80th = stats->percentile;
  _priv(this)->min_delay = stats->min;
}

static void _max_bytes_in_flight(gpointer data, gint64 value)
{
  RMDIProcessor *this = data;
  this->result.max_bytes_in_flight = value;
}

RMDIProcessor *make_rmdi_processor(MPRTPSPath *path)
{
  RMDIProcessor *this;

  this = g_object_new (RMDIPROCESSOR_TYPE, NULL);
  THIS_WRITELOCK (this);

  this->id                     = mprtps_path_get_id(path);
  this->path                   = g_object_ref(path);
  this->made                   = _now(this);
  this->delays                 = make_percentiletracker(600, 50);
  this->packetstracker         = mprtps_path_ref_packetstracker(path);
  this->bytes_in_flight        = make_numstracker(50, 5 * GST_SECOND);

  numstracker_add_plugin(this->bytes_in_flight,
                         (NumsTrackerPlugin*)make_numstracker_minmax_plugin(_max_bytes_in_flight, this, NULL, NULL));

  percentiletracker_set_treshold(this->delays, 60 * GST_SECOND);
  percentiletracker_set_stats_pipe(this->delays, _delay80th_pipe, this);

  _priv(this)->cblocks[0].next = &_priv(this)->cblocks[1];
  _priv(this)->cblocks[1].next = &_priv(this)->cblocks[2];
  _priv(this)->cblocks[2].next = &_priv(this)->cblocks[3];
  _priv(this)->cblocks[3].next = &_priv(this)->cblocks[4];
  _priv(this)->cblocks[4].next = &_priv(this)->cblocks[5];
  _priv(this)->cblocks[5].next = &_priv(this)->cblocks[6];
  _priv(this)->cblocks[6].next = &_priv(this)->cblocks[7];
  _priv(this)->cblocks[0].id   = 0;
  _priv(this)->cblocks[1].id   = 1;
  _priv(this)->cblocks[2].id   = 2;
  _priv(this)->cblocks[3].id   = 3;
  _priv(this)->cblocks[4].id   = 4;
  _priv(this)->cblocks[5].id   = 5;
  _priv(this)->cblocks[6].id   = 6;
  _priv(this)->cblocks[7].id   = 7;

  _priv(this)->cblocks[0].N    = 4;
  _priv(this)->cblocks[1].N    = 4;
  _priv(this)->cblocks[2].N    = 4;
  _priv(this)->cblocks[3].N    = 4;

  _priv(this)->cblocks[4].N    = 4;
  _priv(this)->cblocks[5].N    = 4;
  _priv(this)->cblocks[6].N    = 4;
  _priv(this)->cblocks[7].N    = 4;
  _priv(this)->cblocks_counter = 1;

  THIS_WRITEUNLOCK (this);
  return this;
}


void rmdi_processor_reset(RMDIProcessor *this)
{
  THIS_WRITELOCK (this);
  THIS_WRITEUNLOCK (this);
}


void rmdi_processor_set_acfs_history(RMDIProcessor *this,
                                        gint32 g125_length,
                                        gint32 g250_length,
                                        gint32 g500_length,
                                        gint32 g1000_length)
{
  _priv(this)->cblocks[0].N    = g125_length;
  _priv(this)->cblocks[1].N    = g250_length;
  _priv(this)->cblocks[2].N    = g500_length;
  _priv(this)->cblocks[3].N    = g1000_length;
}

void rmdi_processor_get_acfs_history(RMDIProcessor *this,
                                        gint32 *g125_length,
                                        gint32 *g250_length,
                                        gint32 *g500_length,
                                        gint32 *g1000_length)
{
  if(g125_length){
      *g125_length =  _priv(this)->cblocks[0].N;
  }
  if(g250_length){
      *g250_length =  _priv(this)->cblocks[1].N;
  }
  if(g500_length){
      *g500_length =  _priv(this)->cblocks[2].N;
  }
  if(g1000_length){
      *g1000_length = _priv(this)->cblocks[3].N;
  }
}

static void _process_discarded_rle(RMDIProcessor *this, GstMPRTCPXRReportSummary *xrsummary)
{
  PacketsSndTrackerStat trackerstat;
  packetssndtracker_update_hssn(this->packetstracker, xrsummary->DiscardedRLE.end_seq);
  packetssndtracker_add_discarded_bitvector(this->packetstracker,
                                            xrsummary->DiscardedRLE.begin_seq,
                                            xrsummary->DiscardedRLE.end_seq,
                                            (GstRTCPXRBitvectorChunk *)&xrsummary->DiscardedRLE.chunks[0]);
  packetssndtracker_get_stats(this->packetstracker, &trackerstat);
  if(_cmp_seq(this->last_HSSN, xrsummary->DiscardedRLE.end_seq) < 0){
    this->last_HSSN = xrsummary->DiscardedRLE.end_seq;
  }

  this->result.goodput_bitrate =
      packetssndtracker_get_goodput_bytes_from_acked(this->packetstracker, &this->result.utilized_fraction)<<3;
  this->result.sender_bitrate = trackerstat.sent_bytes_in_1s << 3;
  this->result.sent_packets   = trackerstat.sent_packets_in_1s;
  numstracker_add(this->bytes_in_flight, trackerstat.bytes_in_flight);
}

static void _process_owd(RMDIProcessor *this, GstMPRTCPXRReportSummary *xrsummary)
{
  gint64 impulse;
  if(!xrsummary->OWD.median_delay){
    this->result.g1             = 0.;
    this->result.g2             = 0.;
    this->result.g3             = 0.;
    this->result.g4             = 0.;
    goto done;
  }
  percentiletracker_add(this->delays, xrsummary->OWD.median_delay);
//  impulse = ((gint64)xrsummary->OWD.median_delay - (gint64)_priv(this)->delay80th) / 1000000;
  impulse = (gint64)xrsummary->OWD.median_delay - 2 * GST_SECOND;
  impulse/= 1000;
  _priv(this)->cblocks[0].Iu0 = impulse;
  _execute_corrblocks(_priv(this), _priv(this)->cblocks);
  _execute_corrblocks(_priv(this), _priv(this)->cblocks);
  _priv(this)->cblocks[0].Id1 = impulse;
  this->last_delay            = xrsummary->OWD.median_delay;

  this->result.corrH          = !_priv(this)->delay80th ? 0. : (gdouble)this->last_delay / (gdouble)_priv(this)->delay80th;
  this->result.g1             = _priv(this)->cblocks[0].g;
  this->result.g2             = _priv(this)->cblocks[1].g;
  this->result.g3             = _priv(this)->cblocks[2].g;
  this->result.g4             = _priv(this)->cblocks[3].g;

  _csv_logging(this, impulse);
  _readable_logging(this);
done:
  return;
}

void rmdi_processor_do(RMDIProcessor       *this,
                       GstMPRTCPReportSummary *summary,
                       RMDIProcessorResult *result)
{

  if(!summary->XR.DiscardedRLE.processed && !summary->XR.OWD.processed){
    goto done;
  }
  if(summary->XR.DiscardedRLE.processed){
    _process_discarded_rle(this, &summary->XR);
  }
  if(summary->XR.OWD.processed){
    _process_owd(this, &summary->XR);
  }

  memcpy(result, &this->result, sizeof(RMDIProcessorResult));

  _readable_result(this, result);

done:
  return;

}

//----------------------------------------------------------------------------------------
//                  Queue Delay Analyzation
//----------------------------------------------------------------------------------------


void _execute_corrblocks(RMDIProcessorPrivate *this, CorrBlock *blocks)
{
  guint32 X = (this->cblocks_counter ^ (this->cblocks_counter-1))+1;
  switch(X){
    case 2:
      _execute_corrblock(blocks);
    break;
    case 4:
          _execute_corrblock(blocks + 1);
        break;
    case 8:
          _execute_corrblock(blocks + 2);
        break;
    case 16:
          _execute_corrblock(blocks + 3);
        break;
    case 32:
          _execute_corrblock(blocks + 4);
        break;
    case 64:
          _execute_corrblock(blocks + 5);
        break;
    case 128:
          _execute_corrblock(blocks + 6);
        break;
    case 256:
          _execute_corrblock(blocks + 7);
        break;
    default:
//      g_print("not execute: %u\n", X);
      break;
  }

  ++this->cblocks_counter;
}

void _execute_corrblock(CorrBlock* this)
{
  this->M1   = this->M0;
  this->M0  -= this->M_[this->index];
  this->G01 -= this->G_[this->index];
  this->M0  += this->M_[this->index] = this->Iu0;
  this->G01 += this->G_[this->index] = this->Iu0 * this->Id1;
  if(this->M0 && this->M1){
    this->g  = this->G01 * (this->N);
    this->g /= this->M0 * this->M1;
    this->g -= 1.;
  }else{
    this->g = 0.;
  }
  if(++this->index == this->N) {
    this->index = 0;
  }

  if(this->next){
    CorrBlock *next = this->next;
    next->Iu0 = this->Iu0 + this->Iu1;
    next->Id1 = this->Id2 + this->Id3;
  }

  this->Iu1  = this->Iu0;
  this->Id3  = this->Id2;
  this->Id2  = this->Id1;
}


void _csv_logging(RMDIProcessor *this, gint64 delay)
{
  gchar filename[255];
  memset(filename, 0, 255);
  sprintf(filename, "rmdiautocorrs_%d.csv", this->id);
  mprtp_logger(filename,
               "%ld,%10.8f,%10.8f,%10.8f,%10.8f,%10.8f,%10.8f,%10.8f,%10.8f\n",

               delay,

              _priv(this)->cblocks[0].g,
              _priv(this)->cblocks[1].g,
              _priv(this)->cblocks[2].g,
              _priv(this)->cblocks[3].g,
              _priv(this)->cblocks[4].g,
              _priv(this)->cblocks[5].g,
              _priv(this)->cblocks[6].g,
              _priv(this)->cblocks[7].g

  );
}

void _readable_logging(RMDIProcessor *this)
{
  gchar filename[255];
  memset(filename, 0, 255);
  sprintf(filename, "fbra2helper_%d.log", this->id);
  mprtp_logger(filename,
               "############ Network Queue Analyser log after %lu seconds #################\n"
               "g(0): %-10.8f| g(0): %-10.8f| g(0): %-10.8f| g(0): %-10.8f| g(0): %-10.8f|\n"
               ,

               GST_TIME_AS_SECONDS(_now(this) - this->made),

              _priv(this)->cblocks[0].g,
              _priv(this)->cblocks[1].g,
              _priv(this)->cblocks[2].g,
              _priv(this)->cblocks[3].g

  );
}

void _readable_result(RMDIProcessor *this, RMDIProcessorResult *result)
{
  GstClockTime secs, msecs;
  gchar filename[255];
  memset(filename, 0, 255);
  sprintf(filename, "rmdiproc_%d.log", this->id);
  secs  = GST_TIME_AS_SECONDS(_now(this) - this->made);
  msecs = GST_TIME_AS_MSECONDS(_now(this) - this->made);
  mprtp_logger(filename,
               "############ Receiver Measured Delay Impact Processor Result #################\n"
               "time since started:  %ds (%dms)\n"
               "max_bytes_in_flight: %d\n"
               "sender_bitrate:      %d\n"
               "goodput_bitrate:     %d\n"
               "utilized_fraction:   %f\n"
               "corrH:               %f\n"
               "g_125:               %f\n"
               "g_250:               %f\n"
               "g_500:               %f\n"
               "g_1000:              %f\n"
               ,

               secs,
               msecs,

               result->max_bytes_in_flight,
               result->sender_bitrate,
               result->goodput_bitrate,
               result->utilized_fraction,
               result->corrH,
               result->g1,
               result->g2,
               result->g3,
               result->g4
  );
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

#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
#undef THIS_READLOCK
#undef THIS_READUNLOCK
