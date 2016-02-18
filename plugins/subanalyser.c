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


typedef struct _StatCollector StatCollector;
typedef struct _TrendDetector TrendDetector;
typedef struct _CongestionDetector CongestionDetector;
typedef struct _QueueDelayAnalyzer QueueDelayAnalyzer;
typedef struct{
  gdouble  avg,var,dev,off;
  gint64   max,min,median;
  gboolean trend;
  gboolean congestion;
}ItemsStat;

#define STATCOLLECTOR_ITEMS_LENGTH 4
struct _StatCollector{
  gint64          items[STATCOLLECTOR_ITEMS_LENGTH];
  gint64          sorted[STATCOLLECTOR_ITEMS_LENGTH];
  gint            index,last_forwarded_index;
  gint64          sum,sq_sum;
  ItemsStat       forward;
  gint64          treshold;
  TrendDetector  *tdetector;
};

struct _TrendDetector{
  ItemsStat           last;
  ItemsStat           forward;
  gboolean            filled;
  gint64              treshold;
  CongestionDetector *cdetector;
};

struct _CongestionDetector{
  ItemsStat           last;
  ItemsStat           forward;
  gboolean            filled;
  gint64              treshold;
  QueueDelayAnalyzer* qdelays;
};


struct _QueueDelayAnalyzer{
  StatCollector       collector;
  TrendDetector       tdetector;
  CongestionDetector  cdetector;
  gboolean            congestion,bottleneck,blockage;
  ItemsStat           reference;
};

typedef struct _SubAnalyserPrivate{
  SubAnalyserResult  *result;
  gdouble             delay_target;
  gdouble             off_avg;
  gdouble             delay_avg;
  gdouble             qtrend;
  guint32             sending_rate_median;
  QueueDelayAnalyzer *qanalyzer;
  gchar               logstr[4096];
}SubAnalyserPrivate;

#define _priv(this) ((SubAnalyserPrivate*) this->priv)
#define _result(this) ((SubAnalyserResult*) _priv(this)->result)
#define _now(this) (gst_clock_get_time(this->sysclock))
#define _qdelays(this) ((QueueDelayAnalyzer*) _priv(this)->qanalyzer)
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

static void _queuedelay_analyzator_init(QueueDelayAnalyzer *this);
static void _statcollector_add_queue_delay(StatCollector *this, gint64 queue_delay);
static void _statcollector_forward(StatCollector *this);
static void _tdetector_add_stat(TrendDetector *this, ItemsStat *actual);
static void _cdetector_add_stat(CongestionDetector *this, ItemsStat *actual);
static void _qdeanalyzer_evaluation(QueueDelayAnalyzer *this, ItemsStat *actual);


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
  _priv(this)->qanalyzer = g_malloc0(sizeof(QueueDelayAnalyzer));
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
  _qdelays(this)->blockage      = FALSE;
  _qdelays(this)->bottleneck    = FALSE;
  _qdelays(this)->congestion    = FALSE;
  for(i=0; i<measurement->rle_delays.length; ++i){
    GstClockTime delay;
    gdouble off;
    delay = measurement->rle_delays.values[i];
    if(!delay) continue;
    off = (gdouble) delay / _priv(this)->delay_target;
    off_add+=(_priv(this)->delay_target - (gdouble) delay) / _priv(this)->delay_target;
    numstracker_add(this->De_window, delay);
    floatsbuffer_add(this->DeOff_window, off);

    _statcollector_add_queue_delay(&_qdelays(this)->collector, delay);

    if(0. < (gdouble)delay) _priv(this)->delay_avg = (gdouble)delay * .2 + _priv(this)->delay_avg * .8;
    else                    _priv(this)->delay_avg = (gdouble)delay;

    if(0. < off) _priv(this)->off_avg = off * .1 + _priv(this)->off_avg * .9;
    else         _priv(this)->off_avg = off;
  }

  this->RR_avg = this->RR_avg * .5 + measurement->received_payload_bytes * 4.;

  _statcollector_forward(&_qdelays(this)->collector);

  result->delay_off = off_add;
  result->discards_rate  =  ((gdouble) measurement->late_discarded_bytes / (gdouble) measurement->received_payload_bytes);
  result->qtrend    = _priv(this)->qtrend;
  result->sending_rate_median = _priv(this)->sending_rate_median;

  result->delay_indicators.blockage   = _qdelays(this)->blockage;
  result->delay_indicators.bottleneck = _qdelays(this)->bottleneck;
  result->delay_indicators.congestion = _qdelays(this)->congestion;
  br_ratio = this->RR_avg / (gdouble) _priv(this)->sending_rate_median;
  disc_ratio = ((gdouble) measurement->late_discarded_bytes / (gdouble) measurement->received_payload_bytes);
  tr_ratio = (gdouble) target_bitrate / (gdouble) _priv(this)->sending_rate_median;
  result->rate_indicators.congested     = br_ratio < .1;
  result->rate_indicators.rr_correlated = br_ratio > .9;
  result->rate_indicators.tr_correlated = tr_ratio > .95;
  result->rate_indicators.distorted     = disc_ratio > .1;

}

void subanalyser_append_logfile(SubAnalyser *this, FILE *file)
{

  if(this->append_log_abbr < _now(this) - 60 * GST_SECOND){
    _log_abbrevations(this, file);
    this->append_log_abbr = _now(this);
  }


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

          _qdelays(this)->blockage, _qdelays(this)->bottleneck,
          _qdelays(this)->congestion,

          _priv(this)->logstr
          );
  memset(_priv(this)->logstr, 0, 4096);

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

static void _print_itemsstat(const gchar *title, ItemsStat *stat)
{
g_print("############ %s ########################################################\n"
    "avg: %f| var: %f| dev: %f| davg: %f\n"
    "median: %ld| max: %ld| min: %ld|\n"
    "trend: %d| congestion: %d|\n"
        "###################################################################################################\n",
          title,
          stat->avg, stat->var, stat->dev, stat->off,
          stat->median, stat->max, stat->min,
          stat->trend, stat->congestion
          );
}

void _queuedelay_analyzator_init(QueueDelayAnalyzer *this)
{
  this->collector.tdetector = &this->tdetector;
  this->tdetector.cdetector = &this->cdetector;
  this->cdetector.qdelays   = this;

}

void _statcollector_add_queue_delay(StatCollector *this, gint64 queue_delay)
{
  gint64 obsolated_queue_delay;
  obsolated_queue_delay = this->items[this->index];
  this->items[this->index] = this->sorted[this->index] = queue_delay;
  this->sum-=obsolated_queue_delay;
  this->sum+=queue_delay;
  this->sq_sum -= obsolated_queue_delay * obsolated_queue_delay;
  this->sq_sum += queue_delay * queue_delay;
  if(++this->index == STATCOLLECTOR_ITEMS_LENGTH){
    this->index = 0;
  }
  if(this->index == this->last_forwarded_index){
    _statcollector_forward(this);
  }
  return;
}

#define _swap_sitems(this, i1, i2, temp) \
  temp             = this->sorted[i1];   \
  this->sorted[i1] = this->sorted[i2];   \
  this->sorted[i2] = temp;

void _statcollector_forward(StatCollector *this)
{
  gint64 t;
  this->forward.avg = (gdouble)this->sum / (gdouble) STATCOLLECTOR_ITEMS_LENGTH;
  this->forward.var = (gdouble)STATCOLLECTOR_ITEMS_LENGTH * this->sq_sum;
  this->forward.var -= (gdouble)(this->sum * this->sum);
  this->forward.var /= (gdouble) (STATCOLLECTOR_ITEMS_LENGTH * (STATCOLLECTOR_ITEMS_LENGTH - 1));
  this->forward.dev = sqrt(this->forward.var);


  //select max
  //sorting network for 4 items
  //https://mitpress.mit.edu/sites/default/files/Chapter%2027.pdf
  //if two nodes are connected the upper contains the greater
  //of the two compared value and the downer contains the smaller.
  // a1                    a1'
  // ----o----o-------------
  // a2  |    |            a2'
  // ----o----|----o----o---
  // a3       |    |    |  a3'
  // ----o----o----|----o---
  // a4  |         |       a4'
  // ----o---------o--------

  if(this->sorted[1] < this->sorted[0]){
    _swap_sitems(this, 0, 1, t);
  }
  if(this->sorted[3] < this->sorted[2]){
    _swap_sitems(this, 2, 3, t);
  }
  if(this->sorted[2] < this->sorted[0]){
     _swap_sitems(this, 0, 2, t);
  }
  if(this->sorted[3] < this->sorted[1]){
     _swap_sitems(this, 1, 3, t);
  }
  if(this->sorted[3] < this->sorted[2]){
     _swap_sitems(this, 2, 3, t);
  }

  this->forward.min          = this->sorted[0];
  this->forward.max          = this->sorted[3];
  this->forward.median       = (this->sorted[1] + this->sorted[2])>>1;
  this->forward.trend        = FALSE;
  this->forward.congestion   = FALSE;
  this->forward.off          = (gdouble)(this->treshold - this->forward.median) / (gdouble)this->treshold;

  DISABLE_LINE _print_itemsstat("StatCollector forwarded stat", &this->forward);

  this->last_forwarded_index = this->index;
  _tdetector_add_stat(this->tdetector , &this->forward);
  return;
}


void _tdetector_add_stat(TrendDetector *this, ItemsStat *actual)
{
  ItemsStat  *last;
  last = &this->last;
  if(!this->filled) {
    this->filled = TRUE;
    goto done;
  }
  //trend detection
  actual->trend = (this->treshold < actual->dev);
  if(last->trend || actual->trend){
    this->forward.avg = MIN(last->avg, actual->avg);
    this->forward.var = MIN(last->var, actual->var);
    this->forward.dev = MIN(last->dev, actual->dev);
    this->forward.median = MIN(last->median, actual->median);
  }else{
    this->forward.avg = (last->avg + actual->avg) / 2.;
    this->forward.dev = (last->dev + actual->dev) / 2.;
    this->forward.median = MAX(last->median, actual->median);
    this->forward.var = MAX(last->var, actual->var);
  }
  this->forward.trend  = actual->trend;
  this->forward.max    = MAX(last->max, actual->max);
  this->forward.min    = MIN(last->min, actual->min);

  actual->off   = last->avg - actual->avg;
  _cdetector_add_stat(this->cdetector, &this->forward);
done:
  memcpy(last, actual, sizeof(ItemsStat));
}

void _cdetector_add_stat(CongestionDetector *this, ItemsStat *actual)
{
  ItemsStat *last, *forward = NULL;

  last = &this->last;
  if(!this->filled) {
    this->filled = TRUE;
    forward = actual;
    goto done;
  }

  //congestion detection
  actual->congestion = (this->treshold < actual->median);
  if(actual->congestion) g_print("congestion treshold: %ld median: %ld\n", this->treshold, actual->median);
  if(actual->congestion || actual->trend || last->congestion || last->trend) goto done;

  //reference point
  this->forward.avg = (last->avg + actual->avg) / 2.;
  this->forward.dev = (last->dev + actual->dev) / 2.;
  this->forward.median = MIN(last->median, actual->median);
  this->forward.var = MIN(last->var, actual->var);
  this->forward.max    = MAX(last->max, actual->max);
  this->forward.min    = MIN(last->min, actual->min);
  forward = &this->forward;
done:
  _qdeanalyzer_evaluation(this->qdelays, forward);
  memcpy(last, actual, sizeof(ItemsStat));
}

void _qdeanalyzer_evaluation(QueueDelayAnalyzer *this, ItemsStat *actual)
{
  StatCollector *collector;
  CongestionDetector *cdetector;
  TrendDetector *tdetector;
  ItemsStat *last;

  tdetector = &this->tdetector;
  cdetector = &this->cdetector;
  collector = &this->collector;

  this->blockage      |= tdetector->last.off < 0.;
  this->bottleneck    |= tdetector->last.trend;
  this->congestion    |= cdetector->last.congestion;

  if(!actual) goto done;
  last = &this->reference;
  //new reference point forwarded
  this->reference.avg = (last->avg + actual->avg) / 2.;
  this->reference.dev = (last->dev + actual->dev) / 2.;
  this->reference.median = (last->median + actual->median)>>1;
  this->reference.var = (last->var + actual->var) / 2.;
  this->reference.max    = MIN(last->max, actual->max);
  this->reference.min    = MAX(last->min, actual->min);

  //refresh t and cdetector treshold values
  collector->treshold = .25 * this->reference.min + this->reference.median * .75;
  tdetector->treshold = this->reference.dev * 4.;
  cdetector->treshold = this->reference.median * 2;
done:
  return;
}

#undef _swap_sitems
#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
#undef THIS_READLOCK
#undef THIS_READUNLOCK
