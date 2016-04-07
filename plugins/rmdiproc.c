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
static void _csv_logging(RMDIProcessor *this, GstClockTime delay);
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

RMDIProcessor *make_rmdi_processor(MPRTPSPath *path)
{
  RMDIProcessor *this;

  this = g_object_new (RMDIPROCESSOR_TYPE, NULL);
  THIS_WRITELOCK (this);

  this->id                     = mprtps_path_get_id(path);
  this->path                   = g_object_ref(path);
  this->made                   = _now(this);
  this->delays                 = make_percentiletracker(600, 80);
  this->packetstracker         = make_packetssndtracker();


  mprtps_path_set_packets_tracker(path, this->packetstracker);

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

  _priv(this)->cblocks[0].N    = 8;
  _priv(this)->cblocks[1].N    = 4;
  _priv(this)->cblocks[2].N    = 4;
  _priv(this)->cblocks[3].N    = 8;

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

static void _process_rmdi(RMDIProcessor *this, GstRTCPAFB_RMDI *rmdi)
{
  guint8 records_num;
  gint16 actual_record_index;
  GstClockTime delay;
  guint16 HSSN, disc_packets_num;
  guint32 owd_sample;
  GstRTCPAFB_RMDIRecord *record;

  gst_rtcp_afb_rmdi_getdown(rmdi, NULL, &records_num, NULL);
  actual_record_index = records_num;
again:
  if(--actual_record_index < 0){
    goto done;
  }
  record = &rmdi->records[actual_record_index];
  gst_rtcp_afb_rmdi_record_getdown(record, &HSSN, &disc_packets_num, &owd_sample);
  if(_cmp_seq(HSSN, this->last_HSSN) < 1){
    goto again;
  }

  delay = get_epoch_time_from_ntp_in_ns(((guint64) owd_sample)<<16);
  percentiletracker_add(this->delays, delay);

  _priv(this)->cblocks[0].Iu0 = GST_TIME_AS_USECONDS(delay) / 50.;
  _execute_corrblocks(_priv(this), _priv(this)->cblocks);
  _execute_corrblocks(_priv(this), _priv(this)->cblocks);
  _priv(this)->cblocks[0].Id1 = GST_TIME_AS_USECONDS(delay) / 50.;

  this->last_delay            = delay;
  this->last_disc_packets_num = disc_packets_num;
  this->last_HSSN             = HSSN;

  _csv_logging(this, delay);
  _readable_logging(this);
  goto again;
done:
  return;
}

void rmdi_processor_do(RMDIProcessor       *this,
                          GstMPRTCPReportSummary *summary,
                          RMDIProcessorResult *result)
{
  PacketsSndTrackerStat trackerstat;
  if(!summary->AFB.processed || summary->AFB.fci_id != RTCP_AFB_RMDI_ID){
    goto done;
  }
  _process_rmdi(this, (GstRTCPAFB_RMDI*) summary->AFB.fci_data);


  packetssndtracker_update_hssn(this->packetstracker, this->last_HSSN);
  packetssndtracker_get_stats(this->packetstracker, &trackerstat);


  result->sender_bitrate    = trackerstat.sent_bytes_in_1s * 8;
  result->utilized_rate     = (gdouble)(trackerstat.acked_packets_in_1s - this->last_disc_packets_num) / (gdouble) trackerstat.acked_packets_in_1s;
  result->goodput_bitrate   = trackerstat.acked_bytes_in_1s * result->utilized_rate;
  result->corrH             = !_priv(this)->delay80th ? 0. : (gdouble)this->last_delay / (gdouble)_priv(this)->delay80th;
  result->g_125             = _priv(this)->cblocks[0].g;
  result->g_250             = _priv(this)->cblocks[1].g;
  result->g_500             = _priv(this)->cblocks[2].g;
  result->g_1000            = _priv(this)->cblocks[3].g;

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


void _csv_logging(RMDIProcessor *this, GstClockTime delay)
{
  gchar filename[255];
  memset(filename, 0, 255);
  sprintf(filename, "rmdiautocorrs_%d.csv", this->id);
  mprtp_logger(filename,
               "%lu,%10.8f,%10.8f,%10.8f,%10.8f,%10.8f,%10.8f,%10.8f,%10.8f\n",

               GST_TIME_AS_USECONDS(delay),

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
  gchar filename[255];
  memset(filename, 0, 255);
  sprintf(filename, "fbra2helper_%d.log", this->id);
  mprtp_logger(filename,
               "############ Network Queue Analyser Results #################\n"
               "corrH: %f\n"
               ,

               result->corrH
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
