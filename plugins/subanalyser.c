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
#include <stdlib.h>

//#define THIS_READLOCK(this) g_rw_lock_reader_lock(&this->rwmutex)
//#define THIS_READUNLOCK(this) g_rw_lock_reader_unlock(&this->rwmutex)
//#define THIS_WRITELOCK(this) g_rw_lock_writer_lock(&this->rwmutex)
//#define THIS_WRITEUNLOCK(this) g_rw_lock_writer_unlock(&this->rwmutex)

#define THIS_READLOCK(this)
#define THIS_READUNLOCK(this)
#define THIS_WRITELOCK(this)
#define THIS_WRITEUNLOCK(this)

#define AGGRESSIVITY 0.1f

GST_DEBUG_CATEGORY_STATIC (subanalyser_debug_category);
#define GST_CAT_DEFAULT cosubanalyser_debug_category

G_DEFINE_TYPE (SubAnalyser, subanalyser, G_TYPE_OBJECT);

typedef struct _CorrBlock CorrBlock;

struct _CorrBlock{
  guint           id,N;
  gint64          Iu0,Iu1,Id1,Id2,Id3,G01,M0,M1,G_[16],M_[16];
  gint            index;
  gdouble         g,g1,g_next,g_dev;
  CorrBlock*     next;
};


static void _execute_corrblocks(guint32 *counter, CorrBlock *blocks, guint blocks_length);
static void _execute_corrblock(CorrBlock* this);

typedef struct _SubAnalyserPrivate{
  SubAnalyserResult  *result;
  gdouble             delay_target;
  gdouble             off_avg;
  gdouble             delay_avg,delay_t2,delay_t1,delay_t0;
  gdouble             qtrend;
  gboolean            stability, distortion,congestion;
  CorrBlock           cblocks[4];
  gdouble             qdelays_th;
  guint32             cblocks_counter;
}SubAnalyserPrivate;


#define _priv(this) ((SubAnalyserPrivate*) this->priv)
#define _result(this) ((SubAnalyserResult*) _priv(this)->result)
#define _now(this) (gst_clock_get_time(this->sysclock))
//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------
static void _log_abbrevations(SubAnalyser *this, FILE *file);

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
  g_free(_result(this));
  g_free(_priv(this));
  g_object_unref(this->sysclock);
}

void
subanalyser_init (SubAnalyser * this)
{

}

SubAnalyser *make_subanalyser(void)
{
  SubAnalyser *this;
  this = g_object_new (SUBANALYSER_TYPE, NULL);
  THIS_WRITELOCK (this);
  this->sysclock = gst_system_clock_obtain();
  this->priv = g_malloc0(sizeof(SubAnalyserPrivate));
  _priv(this)->result = g_malloc0(sizeof(SubAnalyserPrivate));
  _priv(this)->delay_target = 100 * GST_MSECOND;

  _priv(this)->cblocks[0].next = &_priv(this)->cblocks[1];
  _priv(this)->cblocks[1].next = &_priv(this)->cblocks[2];
  _priv(this)->cblocks[0].id   = 0;
  _priv(this)->cblocks[1].id   = 1;
  _priv(this)->cblocks[2].id   = 2;
  _priv(this)->cblocks[0].N    = 4;
  _priv(this)->cblocks[1].N    = 4;
  _priv(this)->cblocks[2].N    = 4;
  _priv(this)->cblocks_counter = 1;
  _priv(this)->qdelays_th = .001;
  THIS_WRITEUNLOCK (this);
  return this;
}


void subanalyser_reset(SubAnalyser *this)
{
  THIS_WRITELOCK (this);
  THIS_WRITEUNLOCK (this);
}



void subanalyser_measurement_analyse(SubAnalyser *this,
                                     RRMeasurement *measurement,
                                     gint32 target_bitrate,
                                     gint32 sending_rate,
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

    _priv(this)->cblocks[0].Iu0 = GST_TIME_AS_USECONDS(delay) / 50.;
    _execute_corrblocks(&_priv(this)->cblocks_counter, _priv(this)->cblocks, 4);
    _execute_corrblocks(&_priv(this)->cblocks_counter, _priv(this)->cblocks, 4);
    _priv(this)->cblocks[0].Id1 = GST_TIME_AS_USECONDS(delay) / 50.;

    if(_priv(this)->delay_avg == 0.) _priv(this)->delay_avg = delay;
    else                             _priv(this)->delay_avg = delay * .002 + _priv(this)->delay_avg * .998;

    _priv(this)->delay_t2 = _priv(this)->delay_t1;
    _priv(this)->delay_t1 = _priv(this)->delay_t0;
    _priv(this)->delay_t0 = delay;
  }

  _qdeanalyzer_evaluation(this);
  this->RR_avg = this->RR_avg * .5 + measurement->received_payload_bytes * 4.;

  result->delay_off = off_add;
  result->discards_rate  =  ((gdouble) measurement->late_discarded_bytes / (gdouble) measurement->received_payload_bytes);
  result->sending_rate_median = sending_rate;

  result->off                            = (_priv(this)->delay_t0 * .5 + _priv(this)->delay_t1 * .25 + _priv(this)->delay_t2 * .25) / _priv(this)->delay_avg;
  result->qtrend                         = _priv(this)->qtrend;
  result->delay_indicators.congestion    = _priv(this)->congestion;
  result->delay_indicators.distortion    = _priv(this)->distortion;
  result->delay_indicators.stability     = _priv(this)->stability;

  br_ratio = this->RR_avg / (gdouble) target_bitrate;
  disc_ratio = ((gdouble) measurement->late_discarded_bytes / (gdouble) measurement->received_payload_bytes);
  tr_ratio = (gdouble) target_bitrate / (gdouble) result->sending_rate_median;
  result->rate_indicators.rr_correlated  = br_ratio > .9;
  result->rate_indicators.tr_correlated  = tr_ratio > .95 && tr_ratio < 1.05;
  result->rate_indicators.distortion     = disc_ratio > .1;

}

void subanalyser_append_logfile(SubAnalyser *this, FILE *file)
{
  if(this->append_log_abbr < _now(this) - 60 * GST_SECOND){
    _log_abbrevations(this, file);
    this->append_log_abbr = _now(this);
  }

DISABLE_LINE  goto done;

  fprintf(file,
          "######################## Subflow Measurement Analyser log #######################\n"
          "delay_target:  %-10.3f| off_avg:      %-10.3f| qtrend:       %-10.5f|\n"
          "trouble:       %-10d| congestion:   %-10d| fluctuation       |\n"
          "g^(0):         %-10.6f|g^(1):         %-10.6f|g^(2):         %-10.6f|\n"
          "g_dev^(0):     %-10.6f|g_dev^(1):     %-10.6f|g_dev^(2):     %-10.6f|\n"
          "#################################################################################\n",

          _priv(this)->delay_target / (gdouble)GST_SECOND,
          _priv(this)->off_avg,
          _priv(this)->qtrend,

          _priv(this)->congestion,
          _priv(this)->distortion,

          _priv(this)->cblocks[0].g,
          _priv(this)->cblocks[1].g,
          _priv(this)->cblocks[2].g,

          _priv(this)->cblocks[0].g_dev,
          _priv(this)->cblocks[1].g_dev,
          _priv(this)->cblocks[2].g_dev

          );
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

}


//----------------------------------------------------------------------------------------
//                  Queue Delay Analyzation
//----------------------------------------------------------------------------------------

void _qdeanalyzer_evaluation(SubAnalyser *this)
{
  gdouble g_dev, g_dev1, g_dev2, g_dev3;
  g_dev = MAX(AGGRESSIVITY * .01, _priv(this)->cblocks[0].g_dev );
  _priv(this)->qtrend = 0.;
  if(_priv(this)->cblocks[0].g < - g_dev || g_dev < _priv(this)->cblocks[0].g){
    _priv(this)->qtrend = _priv(this)->cblocks[0].g;
  }

  _priv(this)->qtrend      = CONSTRAIN(-.2, .2, _priv(this)->qtrend);

//  g_dev1 = MIN(AGGRESSIVITY * .01, 4. * _priv(this)->cblocks[0].g_dev);
  g_dev1 = AGGRESSIVITY * .01;
//  g_dev2 = MIN(AGGRESSIVITY * .02, 4. * _priv(this)->cblocks[1].g_dev);
  g_dev2 = AGGRESSIVITY * .02;
//  g_dev3 = MIN(AGGRESSIVITY * .04, 4. * _priv(this)->cblocks[2].g_dev);
  g_dev3 = AGGRESSIVITY * .04;

  _priv(this)->stability  =   _priv(this)->cblocks[0].g_dev != 0. ? -g_dev1 <= _priv(this)->cblocks[0].g    &&   _priv(this)->cblocks[0].g <= g_dev1 : TRUE;
  _priv(this)->stability  &=  _priv(this)->cblocks[1].g_dev != 0. ? -g_dev2 <= _priv(this)->cblocks[1].g    &&   _priv(this)->cblocks[1].g <= g_dev2 : TRUE;
  _priv(this)->stability  &=  _priv(this)->cblocks[1].g_dev != 0. ? -g_dev3 <= _priv(this)->cblocks[2].g    &&   _priv(this)->cblocks[2].g <= g_dev3 : TRUE;

  _priv(this)->distortion  = 0. < _priv(this)->qtrend;
  _priv(this)->congestion  = AGGRESSIVITY < _priv(this)->qtrend;

//  _priv(this)->distortion  = _priv(this)->qtrend < -AGGRESSIVITY      ||      AGGRESSIVITY < _priv(this)->qtrend;
//  _priv(this)->congestion  = _priv(this)->qtrend < -50. * AGGRESSIVITY || 50. * AGGRESSIVITY < _priv(this)->qtrend;
}


void _execute_corrblocks(guint32 *counter, CorrBlock *blocks, guint blocks_length)
{
  guint32 X = (*counter ^ (*counter-1))+1;
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
    default:
//      g_print("not execute: %u\n", X);
      break;
  }

  ++*counter;
}

void _execute_corrblock(CorrBlock* this)
{
  this->M1   = this->M0;
  this->M0  -= this->M_[this->index];
  this->G01 -= this->G_[this->index];
  this->M0  += this->M_[this->index] = this->Iu0;
  this->G01 += this->G_[this->index] = this->Iu0 * this->Id1;
  if(this->M0 && this->M1){
    this->g1 = this->g;
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

  if(this->M0 && this->M1){
    this->g_next = this->g + this->g1;
    this->g_dev += ((this->g - this->g1) - this->g_dev) / (gdouble)(2 * this->N);
    if(this->g_dev < 0.) this->g_dev *=-1.;
  }

}


#undef _swap_sitems
#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
#undef THIS_READLOCK
#undef THIS_READUNLOCK
