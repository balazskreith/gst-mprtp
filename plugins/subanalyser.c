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

typedef struct _SubAnalyserPrivate{
  SubAnalyserResult *result;
  gint64             sr_sum;
  gint64             tr_sum;
  gint64             rr_sum;
  gdouble            rr_dev;
  gdouble            rr_avg;
  gdouble            BiF_avg;
  gdouble            BiF_dev;
  gdouble            delay_dev;
  gdouble            delay_avg;
  guint64            delays80th;
  guint64            delays40th;
  gdouble            delay_target;
  gdouble            delay_trend_dev;
  gdouble            delay_t0,delay_t1,delay_t2,delay_t3;
  gdouble            jitter;
}SubAnalyserPrivate;

#define _priv(this) ((SubAnalyserPrivate*) this->priv)
#define _result(this) ((SubAnalyserResult*) _priv(this)->result)
#define _now(this) (gst_clock_get_time(this->sysclock))
//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void _rr_stat_pipe(gpointer data, NumsTrackerStatData *stat);
static void _sr_stat_pipe(gpointer data, NumsTrackerStatData *stat);
static void _tr_stat_pipe(gpointer data, NumsTrackerStatData *stat);
static void _BiF_stat_pipe(gpointer data, NumsTrackerStatData *stat);
static void _De_stat_pipe(gpointer data, NumsTrackerStatData *stat);
static void _delaysH_stat_pipe(gpointer data, PercentileTrackerPipeData *stat);
static void _delaysL_stat_pipe(gpointer data, PercentileTrackerPipeData *stat);
static void _DeT_stat_pipe(gpointer data, FloatsBufferStatData *stat);
static void _log_abbrevations(SubAnalyser *this, FILE *file);

static void subanalyser_finalize (GObject * object);

//pipes


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
  this->DeT_window = make_floatsbuffer(length, obsolation_treshold);
  this->BiF_window = make_numstracker(length, obsolation_treshold);
  this->RR_window = make_numstracker(length, obsolation_treshold);
  this->SR_window = make_numstracker(length, obsolation_treshold);
  this->TR_window = make_numstracker(length, obsolation_treshold);
  this->De_window = make_numstracker(length, obsolation_treshold);
  this->delaysH = make_percentiletracker(100, 80);
  percentiletracker_set_treshold(this->delaysH, GST_SECOND * 60);
  this->delaysL = make_percentiletracker(100, 40);
  percentiletracker_set_treshold(this->delaysL, GST_SECOND * 60);

  numstracker_add_plugin(this->RR_window,
                         (NumsTrackerPlugin*) make_numstracker_stat_plugin(_rr_stat_pipe, this));
  numstracker_add_plugin(this->SR_window,
                         (NumsTrackerPlugin*) make_numstracker_stat_plugin(_sr_stat_pipe, this));
  numstracker_add_plugin(this->TR_window,
                         (NumsTrackerPlugin*) make_numstracker_stat_plugin(_tr_stat_pipe, this));
  numstracker_add_plugin(this->BiF_window,
                         (NumsTrackerPlugin*) make_numstracker_stat_plugin(_BiF_stat_pipe, this));
  numstracker_add_plugin(this->De_window,
                         (NumsTrackerPlugin*) make_numstracker_stat_plugin(_De_stat_pipe, this));

  percentiletracker_set_stats_pipe(this->delaysH, _delaysH_stat_pipe, this);
  percentiletracker_set_stats_pipe(this->delaysL, _delaysL_stat_pipe, this);
  floatsbuffer_set_stats_pipe(this->DeT_window, _DeT_stat_pipe, this);
  this->target_aim = 3;
  THIS_WRITEUNLOCK (this);
  return this;
}


void subanalyser_reset(SubAnalyser *this)
{
  THIS_WRITELOCK (this);
  floatsbuffer_reset(this->DeT_window);
  numstracker_reset(this->BiF_window);
  numstracker_reset(this->RR_window);
  numstracker_reset(this->TR_window);
  numstracker_reset(this->SR_window);
  numstracker_reset(this->De_window);
  percentiletracker_reset(this->delaysH);
  percentiletracker_reset(this->delaysL);
  this->target_aim = 1;
  THIS_WRITEUNLOCK (this);
}

void subanalyser_measurement_analyse(SubAnalyser *this,
                                     RRMeasurement *measurement,
                                     gint32 target_bitrate,
                                     SubAnalyserResult *result)
{
  gint i;

  numstracker_add(this->BiF_window, measurement->bytes_in_flight_acked * 8);
  numstracker_add(this->RR_window, measurement->received_payload_bytes * 8);
  numstracker_add(this->SR_window, measurement->sent_payload_bytes * 8);
  numstracker_add(this->TR_window, target_bitrate);

  //add delays
  for(i=0; i<measurement->rle_delays.length; ++i){
    GstClockTime delay;
    delay = measurement->rle_delays.values[i];
    if(!delay) continue;
    numstracker_add(this->De_window, delay);
    _priv(this)->delay_t3 = _priv(this)->delay_t2;
    _priv(this)->delay_t2 = _priv(this)->delay_t1;
    _priv(this)->delay_t1 = _priv(this)->delay_t0;
    _priv(this)->delay_t0 = delay;
  }

  _priv(this)->jitter = MIN(measurement->jitter, _priv(this)->delay_t0 / 10.);

  result->DeCorrT   = (_priv(this)->delay_t0 * .5 + _priv(this)->delay_t1 * .25 + _priv(this)->delay_t2 * .25) / (_priv(this)->delay_target);
  result->DeCorrH   = (gdouble) measurement->recent_delay / (gdouble) _priv(this)->delays80th;
  result->DeCorrL   = (gdouble) measurement->recent_delay / (gdouble) _priv(this)->delays40th;
  result->RateCorr  = (gdouble) _priv(this)->rr_sum / (gdouble) _priv(this)->sr_sum;
  result->TRateCorr = (gdouble) _priv(this)->sr_sum / (gdouble) _priv(this)->tr_sum;
  result->BiFCorr   =  (gdouble)(measurement->bytes_in_flight_acked * 8)/(gdouble) (_priv(this)->BiF_avg + _priv(this)->BiF_dev);

  floatsbuffer_add_full(this->DeT_window, result->DeCorrT, 0);
  result->DeCorrT_dev = _priv(this)->delay_trend_dev;
}

void subanalyser_measurement_add_to_reference(SubAnalyser *this, RRMeasurement *measurement)
{
  gint i;
  for(i=0; i<measurement->rle_delays.length; ++i){
    GstClockTime delay;
    delay = measurement->rle_delays.values[i];
    if(!delay) continue;
    delay+= g_random_int_range(0, 1000);
    percentiletracker_add(this->delaysH, delay);
    percentiletracker_add(this->delaysL, delay);
  }
}

void subanalyser_append_logfile(SubAnalyser *this, FILE *file)
{
  if(this->append_log_abbr < _now(this) - 60 * GST_SECOND){
    _log_abbrevations(this, file);
    this->append_log_abbr = _now(this);
  }
  fprintf(file,
          "######################## Subflow Measurement Analyser log #######################\n"
          "delay_t0: %-10.3f| delay_t1: %-10.3f| delay_t2: %-10.3f| delay_t3: %-10.3f|\n"
          "sr_sum:   %-10ld| rr_sum: %-10ld| BiF_avg: %-10.3f| BiF_dev: %-10.3f|\n"
          "tdelay:   %-10.3f| delay80: %-10.3f| delay40: %-10.3f| DeT_dev: %-10.3f\n"
          "jitter:   %-10.3f|\n"
          "#################################################################################\n",

          _priv(this)->delay_t0 / (gdouble)GST_SECOND
          ,_priv(this)->delay_t1 / (gdouble)GST_SECOND,
          _priv(this)->delay_t2 / (gdouble)GST_SECOND
          ,_priv(this)->delay_t3 / (gdouble)GST_SECOND,

          _priv(this)->sr_sum, _priv(this)->rr_sum,
          _priv(this)->BiF_avg, _priv(this)->BiF_dev,

          _priv(this)->delay_target / (gdouble)GST_SECOND,
          (gdouble)_priv(this)->delays80th / (gdouble)GST_SECOND,
          (gdouble)_priv(this)->delays40th / (gdouble)GST_SECOND,
          _priv(this)->delay_trend_dev,

          _priv(this)->jitter

          );

}

void _log_abbrevations(SubAnalyser *this, FILE *file)
{
  fprintf(file,
  "############ Subflow Analyser abbrevations ########################################################\n"
  "#  delay_t0:   receiver reported delay now                                                        #\n"
  "#  delay_t1:   receiver reported delay 1s ago                                                     #\n"
  "#  delay_t2:   receiver reported delay 2s ago                                                     #\n"
  "#  delay_t3:   receiver reported delay 3s ago                                                     #\n"
  "#  sr_sum:     Calculated sum of the sender rate                                                  #\n"
  "#  rr_sum:     Calculated sum of the receiver report based on reports                             #\n"
  "#  BiF_avg:    Average of the reported bytes in flights                                           #\n"
  "#  BiF_dev:    Deviation of bytes in flights                                                      #\n"
  "#  tdelay:     Target delay                                                                       #\n"
  "###################################################################################################\n"
  );
}

void _rr_stat_pipe(gpointer data, NumsTrackerStatData *stat)
{
  SubAnalyser *this = data;
  _priv(this)->rr_sum = stat->sum;
  _priv(this)->rr_dev = stat->dev;
  _priv(this)->rr_avg = stat->avg;
}

void _sr_stat_pipe(gpointer data, NumsTrackerStatData *stat)
{
  SubAnalyser *this = data;
  _priv(this)->sr_sum = stat->sum;
}

void _tr_stat_pipe(gpointer data, NumsTrackerStatData *stat)
{
  SubAnalyser *this = data;
  _priv(this)->tr_sum = stat->sum;
}

void _BiF_stat_pipe(gpointer data, NumsTrackerStatData *stat)
{
  SubAnalyser *this = data;
  _priv(this)->BiF_avg = stat->avg;
  _priv(this)->BiF_dev = stat->dev;
}

void _De_stat_pipe(gpointer data, NumsTrackerStatData *stat)
{
  SubAnalyser *this = data;
  DISABLE_LINE g_print("%p\n", this);
  _priv(this)->delay_dev = stat->dev;
  _priv(this)->delay_avg = stat->avg;
}


void _delaysH_stat_pipe(gpointer data, PercentileTrackerPipeData *stat)
{
  SubAnalyser *this = data;
  _priv(this)->delays80th = stat->percentile;
  if(this->target_aim == 3)
     _priv(this)->delay_target = _priv(this)->delays40th * .125 + _priv(this)->delays80th * .875;
  else if(this->target_aim == 2)
     _priv(this)->delay_target = _priv(this)->delays40th * .375 + _priv(this)->delays80th * .625;
  else
    _priv(this)->delay_target = _priv(this)->delays40th * .625 + _priv(this)->delays80th * .375;
}

void _delaysL_stat_pipe(gpointer data, PercentileTrackerPipeData *stat)
{
  SubAnalyser *this = data;
  _priv(this)->delays40th = stat->percentile;
  if(this->target_aim == 3)
     _priv(this)->delay_target = _priv(this)->delays40th * .125 + _priv(this)->delays80th * .875;
  else if(this->target_aim == 2)
     _priv(this)->delay_target = _priv(this)->delays40th * .375 + _priv(this)->delays80th * .625;
  else
    _priv(this)->delay_target = _priv(this)->delays40th * .625 + _priv(this)->delays80th * .375;
}

void _DeT_stat_pipe(gpointer data, FloatsBufferStatData *stat)
{
  SubAnalyser *this = data;
  _priv(this)->delay_trend_dev = stat->dev;
}


#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
#undef THIS_READLOCK
#undef THIS_READUNLOCK
