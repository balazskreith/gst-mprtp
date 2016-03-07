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
  gdouble         g,g1,g_next,g_dev,distortion_th;
  gboolean        distorted;
  CorrBlock*     next;
};
#define CORR_LOG_ON

static void _execute_corrblocks(guint32 *counter, CorrBlock *blocks, guint blocks_length);
static void _execute_corrblock(CorrBlock* this);

typedef void (*Evaluator)(SubAnalyser *this, SubAnalyserResult *result);

typedef struct _SubAnalyserPrivate{
  SubAnalyserResult  *result;
#ifdef CORR_LOG_ON
  gchar               logfile[255];
#endif
  gdouble             delay_target;
  gdouble             off_avg;
  gdouble             delay_avg,delay_t2,delay_t1,delay_t0;
  CorrBlock           cblocks[6];
  guint32             cblocks_counter;
//Todo: memory check here. WHo is responsible for the memory leak? you? you? you?...

}SubAnalyserPrivate;


#define _priv(this) ((SubAnalyserPrivate*) this->priv)
#define _result(this) ((SubAnalyserResult*) _priv(this)->result)
#define _now(this) (gst_clock_get_time(this->sysclock))
//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------
static void _log_abbrevations(SubAnalyser *this, FILE *file);

static void subanalyser_finalize (GObject * object);

static void _qdeanalyzer_stable_evaluation(SubAnalyser *this, SubAnalyserResult *result);

static void _append_to_corrlog(SubAnalyser *this, const gchar * format, ...);

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

#ifdef CORR_LOG_ON
  {
    FILE *f;
    sprintf(_priv(this)->logfile, "logs/subanalyser.csv");
    f = fopen(_priv(this)->logfile, "w");
    fclose(f);
  }
#endif

  _priv(this)->result = g_malloc0(sizeof(SubAnalyserPrivate));
  _priv(this)->delay_target = 100 * GST_MSECOND;

  _priv(this)->cblocks[0].next = &_priv(this)->cblocks[1];
  _priv(this)->cblocks[1].next = &_priv(this)->cblocks[2];
  _priv(this)->cblocks[2].next = &_priv(this)->cblocks[3];
  _priv(this)->cblocks[3].next = &_priv(this)->cblocks[4];
  _priv(this)->cblocks[0].id   = 0;
  _priv(this)->cblocks[1].id   = 1;
  _priv(this)->cblocks[2].id   = 2;
  _priv(this)->cblocks[3].id   = 3;
  _priv(this)->cblocks[4].id   = 4;
  _priv(this)->cblocks[0].N    = 4;
  _priv(this)->cblocks[1].N    = 4;
  _priv(this)->cblocks[2].N    = 4;
  _priv(this)->cblocks[3].N    = 4;
  _priv(this)->cblocks[4].N    = 4;
  _priv(this)->cblocks[0].distortion_th   = 0.025;
  _priv(this)->cblocks[1].distortion_th   = 0.05;
  _priv(this)->cblocks[2].distortion_th   = 0.1;
  _priv(this)->cblocks[3].distortion_th   = 1.;
  _priv(this)->cblocks[4].distortion_th   = 1.;
  _priv(this)->cblocks_counter = 1;

  THIS_WRITEUNLOCK (this);
  return this;
}


void subanalyser_reset(SubAnalyser *this)
{
  THIS_WRITELOCK (this);
  THIS_WRITEUNLOCK (this);
}

void subanalyser_reset_stability(SubAnalyser *this)
{
  this->last_stable = 0;
}

void subanalyser_measurement_analyse(SubAnalyser *this,
                                     RRMeasurement *measurement,
                                     gint32 target_bitrate,
                                     gint32 sending_rate,
                                     SubAnalyserResult *result)
{
  gint i;
  gdouble tr_ratio;
  gboolean congestion;

  congestion = 0 < measurement->rle_delays.length;
  //add delays
  for(i=0; i<measurement->rle_delays.length; ++i){
    GstClockTime delay;
    delay = measurement->rle_delays.values[i];
    if(!delay){
        congestion &= FALSE;
        continue;
    }
    congestion &= delay == GST_SECOND;

    _priv(this)->cblocks[0].Iu0 = GST_TIME_AS_USECONDS(delay) / 50.;
    _execute_corrblocks(&_priv(this)->cblocks_counter, _priv(this)->cblocks, 4);
    _execute_corrblocks(&_priv(this)->cblocks_counter, _priv(this)->cblocks, 4);
    _priv(this)->cblocks[0].Id1 = GST_TIME_AS_USECONDS(delay) / 50.;

    _append_to_corrlog(this, "%lu,%f,%f,%f,%f,%f,%d\n",
                       delay,
                       _priv(this)->cblocks[0].g,
                       _priv(this)->cblocks[1].g,
                       _priv(this)->cblocks[2].g,
                       _priv(this)->cblocks[3].g,
                       _priv(this)->cblocks[4].g,
                       sending_rate);

    if(_priv(this)->delay_avg == 0.) _priv(this)->delay_avg = delay;
    else                             _priv(this)->delay_avg = delay * .002 + _priv(this)->delay_avg * .998;

    _priv(this)->delay_t2 = _priv(this)->delay_t1;
    _priv(this)->delay_t1 = _priv(this)->delay_t0;
    _priv(this)->delay_t0 = delay;
  }
  result->sending_rate_median     = sending_rate;
  tr_ratio                        = (gdouble) target_bitrate / (gdouble) result->sending_rate_median;
  result->tr_correlated           = tr_ratio > .9 && tr_ratio < 1.1;
  result->discards_rate           =  ((gdouble) measurement->late_discarded_bytes / (gdouble) measurement->received_payload_bytes);
  result->sending_rate_median     = sending_rate;
  result->off                     = (_priv(this)->delay_t0 * .5 + _priv(this)->delay_t1 * .25 + _priv(this)->delay_t2 * .25) / _priv(this)->delay_avg;

  result->pierced = result->distorted = result->congested = congestion;
  _qdeanalyzer_stable_evaluation(this, result);
//  _priv(this)->evaluator(this, result);

}

void _append_to_corrlog(SubAnalyser *this, const gchar * format, ...)
{
  FILE *file;
  va_list args;
#ifdef CORR_LOG_ON
  file = fopen(_priv(this)->logfile, "a");
  va_start (args, format);
  vfprintf (file, format, args);
  va_end (args);
  fclose(file);
#endif
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
          "delay_target:  %-10.3f| off_avg:      %-10.3f|\n"
          "g^(0):         %-10.6f|g^(1):         %-10.6f|g^(2):         %-10.6f|\n"
          "g_dev^(0):     %-10.6f|g_dev^(1):     %-10.6f|g_dev^(2):     %-10.6f|\n"
          "#################################################################################\n",

          _priv(this)->delay_target / (gdouble)GST_SECOND,
          _priv(this)->off_avg,

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

void _qdeanalyzer_stable_evaluation(SubAnalyser *this, SubAnalyserResult *result)
{

  result->pierced   |= result->discards_rate > 0.1 || _priv(this)->cblocks[0].distorted || _priv(this)->cblocks[2].g < -.01;
  result->distorted |= result->pierced   && _priv(this)->cblocks[1].distorted;
  result->congested |= result->distorted && _priv(this)->cblocks[2].distorted;

  _priv(this)->cblocks[0].distorted = FALSE;
  _priv(this)->cblocks[1].distorted = FALSE;
  _priv(this)->cblocks[2].distorted = FALSE;

  if(result->pierced || result->distorted || result->congested){
    result->stable = 0;
    this->last_stable = 0;
  }else if(this->last_stable == 0){
    this->last_stable = _now(this);
    result->stable = 0;
  }else{
    result->stable = GST_TIME_AS_SECONDS(_now(this) - this->last_stable);
  }

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
    case 32:
          _execute_corrblock(blocks + 4);
        break;
    case 64:
          _execute_corrblock(blocks + 5);
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
  if(this->distortion_th < this->g){
    this->distorted = TRUE;
  }
}

#undef CORR_LOG_ON
#undef _swap_sitems
#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
#undef THIS_READLOCK
#undef THIS_READUNLOCK
