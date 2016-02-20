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
#include "subanalyser.h"
#include <math.h>
#include <string.h>

//#define THIS_READLOCK(this) g_rw_lock_reader_lock(&this->rwmutex)
//#define THIS_READUNLOCK(this) g_rw_lock_reader_unlock(&this->rwmutex)
//#define THIS_WRITELOCK(this) g_rw_lock_writer_lock(&this->rwmutex)
//#define THIS_WRITEUNLOCK(this) g_rw_lock_writer_unlock(&this->rwmutex)

#define THIS_READLOCK(this)
#define THIS_READUNLOCK(this)
#define THIS_WRITELOCK(this)
#define THIS_WRITEUNLOCK(this)


GST_DEBUG_CATEGORY_STATIC (subanalyser_debug_category);
#define GST_CAT_DEFAULT cosubanalyser_debug_category

G_DEFINE_TYPE (SubAnalyser, subanalyser, G_TYPE_OBJECT);

typedef struct _CorrBlock CorrBlock;

struct _CorrBlock{
  guint           id,N;
  gint64          Iu0,Iu1,Id1,Id2,Id3,G01,M0,M1,G_[4],M_[4];
  gint            index;
  gdouble         g,g_avg;
  guint           overused;
  gboolean        distortion;
  CorrBlock*     next;
};


static void _execute_corrblocks(guint32 *counter, CorrBlock *blocks, guint blocks_length);
static void _execute_corrblock6(CorrBlock* this);

typedef struct _SubAnalyserPrivate{
  SubAnalyserResult  *result;
  gdouble             delay_target;
  gdouble             off_avg;
  gdouble             delay_avg;
  gdouble             qtrend;
  guint32             sending_rate_median;
  gboolean            congestion,bottleneck,blockage;
  gchar               logstr[4096];
  CorrBlock           cblocks[4];
  guint32             cblocks_counter;
}SubAnalyserPrivate;


#define _priv(this) ((SubAnalyserPrivate*) this->priv)
#define _result(this) ((SubAnalyserResult*) _priv(this)->result)
#define _now(this) (gst_clock_get_time(this->sysclock))
//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------
static void _SR_stat_pipe(gpointer data, PercentileTrackerPipeData *stat);
static void _De_stat_pipe(gpointer data, NumsTrackerStatData *stat);
void _DeOff_state_pipe(gpointer data, FloatsBufferStatData *stat);
static void _De_min_pipe(gpointer data, gint64 min);
//static void _delaysH_stat_pipe(gpointer data, PercentileTrackerPipeData *stat);
//static void _delaysL_stat_pipe(gpointer data, PercentileTrackerPipeData *stat);
static void _log_abbrevations(SubAnalyser *this, FILE *file);
static void _append_log (SubAnalyser *this, const gchar * format, ... );

static void subanalyser_finalize (GObject * object);

static void _qdeanalyzer_evaluation(SubAnalyser *this);


void
subanalyser_class_init (SubAnalyserClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = subanalyser_finalize;

  GST_DEBUG_CATEGORY_INIT (subanalyser_debug_category, "subanalyser", 0,
      "SubAnalyser");

}

void
subanalyser_finalize (GObject * object)
{
  SubAnalyser *this;
  this = SUBANALYSER(object);
  g_free(_qdelays(this));
  g_free(_result(this));
  g_free(_priv(this));
  g_object_unref(this->sysclock);
}

void
subanalyser_init (SubAnalyser * this)
{

}

SubAnalyser *make_subanalyser(
    guint32 length,
    GstClockTime obsolation_treshold)
{
  SubAnalyser *this;
  this = g_object_new (SUBANALYSER_TYPE, NULL);
  THIS_WRITELOCK (this);
  this->sysclock = gst_system_clock_obtain();
  this->priv = g_malloc0(sizeof(SubAnalyserPrivate));
  _priv(this)->result = g_malloc0(sizeof(SubAnalyserPrivate));
  this->SR_window = make_percentiletracker(128, 50);
  percentiletracker_set_treshold(this->SR_window, 2 * GST_SECOND);
  this->De_window = make_numstracker(length, GST_SECOND * 30);
  this->DeOff_window = make_floatsbuffer(20, 3 * GST_SECOND);

  percentiletracker_set_stats_pipe(this->SR_window, _SR_stat_pipe, this);
  numstracker_add_plugin(this->De_window,
                         (NumsTrackerPlugin*) make_numstracker_stat_plugin(_De_stat_pipe, this));
  numstracker_add_plugin(this->De_window,
                         (NumsTrackerPlugin*) make_numstracker_minmax_plugin(NULL, NULL, _De_min_pipe, this));
  floatsbuffer_set_stats_pipe(this->DeOff_window, _DeOff_state_pipe, this);
  _priv(this)->delay_target = 100 * GST_MSECOND;

  _queuedelay_analyzator_init(_qdelays(this));

  _priv(this)->cblocks[0].next = &_priv(this)->cblocks[1];
  _priv(this)->cblocks[1].next = &_priv(this)->cblocks[2];
  _priv(this)->cblocks[2].next = &_priv(this)->cblocks[3];
  _priv(this)->cblocks[0].id   = 0;
  _priv(this)->cblocks[1].id   = 1;
  _priv(this)->cblocks[2].id   = 2;
  _priv(this)->cblocks[3].id   = 3;
  _priv(this)->cblocks[0].N   = 4;
  _priv(this)->cblocks[1].N   = 4;
  _priv(this)->cblocks[2].N   = 4;
  _priv(this)->cblocks[3].N   = 4;
  _priv(this)->cblocks_counter = 1;

  THIS_WRITEUNLOCK (this);
  return this;
}


void subanalyser_reset(SubAnalyser *this)
{
  THIS_WRITELOCK (this);
  percentiletracker_reset(this->SR_window);
  numstracker_reset(this->De_window);
  floatsbuffer_reset(this->DeOff_window);
  THIS_WRITEUNLOCK (this);
}

void subanalyser_time_update(SubAnalyser *this, MPRTPSPath *path)
{
  percentiletracker_add(this->SR_window, mprtps_path_get_sent_bytes_in1s(path, NULL) * 8);
}

void subanalyser_measurement_analyse(SubAnalyser *this,
                                     RRMeasurement *measurement,
                                     gint32 target_bitrate,
                                     SubAnalyserResult *result)
{
  gint i;
  gdouble off_add = 0.;
  gdouble br_ratio,disc_ratio, tr_ratio;
  //add delays
  for(i=0; i<measurement->rle_delays.length; ++i){
    GstClockTime delay;
    delay = measurement->rle_delays.values[i];
    if(!delay) continue;

    _priv(this)->cblocks[0].Iu0 = GST_TIME_AS_USECONDS(delay);
    _execute_corrblocks(&_priv(this)->cblocks_counter, _priv(this)->cblocks, 4);
    _execute_corrblocks(&_priv(this)->cblocks_counter, _priv(this)->cblocks, 4);
    _priv(this)->cblocks[0].Id1 = GST_TIME_AS_USECONDS(delay);

  }
  _qdeanalyzer_evaluation(this);

  this->RR_avg = this->RR_avg * .5 + measurement->received_payload_bytes * 4.;

  result->delay_off = off_add;
  result->discards_rate  =  ((gdouble) measurement->late_discarded_bytes / (gdouble) measurement->received_payload_bytes);
  result->qtrend    = _priv(this)->qtrend;
  result->sending_rate_median = _priv(this)->sending_rate_median;

  result->delay_indicators.blockage   = _priv(this)->blockage;
  result->delay_indicators.bottleneck = _priv(this)->bottleneck;
  result->delay_indicators.congestion = _priv(this)->congestion;
  br_ratio = this->RR_avg / (gdouble) target_bitrate;
  disc_ratio = ((gdouble) measurement->late_discarded_bytes / (gdouble) measurement->received_payload_bytes);
  tr_ratio = (gdouble) target_bitrate / (gdouble) _priv(this)->sending_rate_median;
  result->rate_indicators.congested     = br_ratio < .1;
  result->rate_indicators.rr_correlated = br_ratio > .9;
  result->rate_indicators.tr_correlated = tr_ratio > .95 && tr_ratio < 1.05;
  result->rate_indicators.distorted     = disc_ratio > .2;

}

void subanalyser_append_logfile(SubAnalyser *this, FILE *file)
{

  if(this->append_log_abbr < _now(this) - 60 * GST_SECOND){
    _log_abbrevations(this, file);
    this->append_log_abbr = _now(this);
  }

  fprintf(file,
          "g[0]:   %-10.6f%-10.6f (%d)| g[1]:  %-10.6f%-10.6f (%d)| g[2]:  %-10.6f%-10.6f (%d)| g[3]:  %-10.6f%-10.6f (%d)|\n",
          _priv(this)->cblocks[0].g,_priv(this)->cblocks[0].g_avg,_priv(this)->cblocks[0].distortion,
          _priv(this)->cblocks[1].g,_priv(this)->cblocks[1].g_avg,_priv(this)->cblocks[1].distortion,
          _priv(this)->cblocks[2].g,_priv(this)->cblocks[2].g_avg,_priv(this)->cblocks[2].distortion,
          _priv(this)->cblocks[3].g,_priv(this)->cblocks[3].g_avg,_priv(this)->cblocks[3].distortion
          );


  goto done;

  fprintf(file,
          "######################## Subflow Measurement Analyser log #######################\n"
          "delay_target:  %-10.3f| off_avg:      %-10.3f| qtrend:       %-10.5f|\n"
          "sr_median:     %-10u|\n"
          "blockage:      %-10d| bottleneck:    %-10d| congestion:   %-10d|\n"
          "#################################################################################\n"
          "%s",

          _priv(this)->delay_target / (gdouble)GST_SECOND,
          _priv(this)->off_avg,
          _priv(this)->qtrend,
          _priv(this)->sending_rate_median,

          _priv(this)->blockage, _priv(this)->bottleneck,
          _priv(this)->congestion,

          _priv(this)->logstr
          );
  memset(_priv(this)->logstr, 0, 4096);
done:
  return;
}

void _log_abbrevations(SubAnalyser *this, FILE *file)
{
  fprintf(file,
  "############ Subflow Analyser abbrevations ########################################################\n"
  "NOT COMPLETED YET\n"
  "###################################################################################################\n"
  );

  DISABLE_LINE _append_log(this, "Muhaha");
}

void _append_log (SubAnalyser *this, const gchar * format, ... )
{
  va_list args;
  va_start (args, format);
  vsprintf (_priv(this)->logstr,format, args);
  va_end (args);
}

void _SR_stat_pipe(gpointer data, PercentileTrackerPipeData *stat)
{
  SubAnalyser *this = data;
  _priv(this)->sending_rate_median = stat->percentile;
}


void _De_stat_pipe(gpointer data, NumsTrackerStatData *stat)
{
  SubAnalyser *this = data;
  _priv(this)->delay_avg = stat->avg;
}

void _DeOff_state_pipe(gpointer data, FloatsBufferStatData *stat)
{
  SubAnalyser *this = data;
  if(0. == stat->G0){
    _priv(this)->qtrend = 0.;
    return;
  }
//  _priv(this)->qtrend = MIN(1.0,MAX(0.0,stat->G1 / stat->G0 * _priv(this)->off_avg));
  _priv(this)->qtrend = MIN(2.0,MAX(0.0,stat->G1 / stat->G0 * _priv(this)->off_avg));
//  _priv(this)->qtrend = MIN(1.0,MAX(0.0,stat->G0 / stat->G1));
}

void _De_min_pipe(gpointer data, gint64 min)
{
  SubAnalyser *this = data;
  _priv(this)->delay_target = MIN(min, 400 * GST_MSECOND);
}


//----------------------------------------------------------------------------------------
//                  Queue Delay Analyzation
//----------------------------------------------------------------------------------------

void _qdeanalyzer_evaluation(SubAnalyser *this)
{

  _priv(this)->blockage      = _priv(this)->cblocks[0].distortion;
  _priv(this)->bottleneck    = _priv(this)->cblocks[1].distortion;
  _priv(this)->congestion    = FALSE;

}


void _execute_corrblocks(guint32 *counter, CorrBlock *blocks, guint blocks_length)
{
  guint32 X = (*counter ^ (*counter-1))+1;
  switch(X){
    case 2:
      _execute_corrblock6(blocks);
    break;
    case 4:
          _execute_corrblock6(blocks + 1);
        break;
    case 8:
          _execute_corrblock6(blocks + 2);
        break;
    case 16:
          _execute_corrblock6(blocks + 3);
        break;
    default:
//      g_print("not execute: %u\n", X);
      break;
  }

  ++*counter;
}

void _execute_corrblock6(CorrBlock* this)
{
  this->M1   = this->M0;
  this->M0  -= this->M_[this->index];
  this->G01 -= this->G_[this->index];
  this->M0  += this->M_[this->index] = this->Iu0;
  this->G01 += this->G_[this->index] = this->Iu0 * this->Id1;
  if(this->M0 && this->M1){
    this->g  = this->G01 * this->N;
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

  if(0 < this->overused) {
    --this->overused;
  }else if(this->M0 && this->M1){
    this->g_avg = this->g * .2 + this->g_avg * .8;
  }

  if(this->g_avg * 4. < this->g){
    this->overused = 4;
  }
  this->distortion = 0 < this->overused;
}


#undef _swap_sitems
#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
#undef THIS_READLOCK
#undef THIS_READUNLOCK
