#include "swperctester.h"
#include <math.h>
#include "slidingwindow.h"
#include "lib_swplugins.h"
#include <stdlib.h>

static void _on_percentile_1_calced(gint32 *result, swpercentilecandidates_t *candidates)
{
  gint32 perc,minres,maxres;
  PercentileRawResult(gint32,   \
                   candidates,  \
                   perc, \
                   minres,    \
                   maxres,    \
                   0            \
                   );
  if(0) {
    *result = minres + maxres;
  }
  *result = perc;
}

static void _on_percentile_2_calced(gint32 *result, gint32 *perc)
{
  *result = *perc;
}

static gint32* _mean_calcer(gint32* a, gint32* b) {
  gint32* result = g_malloc(sizeof(gint32));
  *result = (*a + *b) / 2;
  return result;
}

static gint32* _extractor(gint32* source) {
  return source;
}

static gint32 bintree3cmp_pointer_int32(gint32** a, gint32** b)
{
  return bintree3cmp_int32(*a, *b);
}


static void _sw_test_performance(gint32 percentile, gint32 runs, gint32 max_random_number, gint sw_size,
    GstClockTime *performance1, GstClockTime *performance2, GstClockTime* performance3) {
  gint32 perc1_result, perc2_result, perc3_result, i;
  SlidingWindow* sw1 = make_slidingwindow(sw_size, GST_SECOND);
  SlidingWindow* sw2 = make_slidingwindow(sw_size, GST_SECOND);
  SlidingWindow* sw3 = make_slidingwindow(sw_size, GST_SECOND);
  GstClock* clock = gst_system_clock_obtain();
  slidingwindow_add_plugins(sw1,
      make_swpercentile(percentile,
          bintree3cmp_int32,
          (ListenerFunc)
          _on_percentile_1_calced,
          &perc1_result),
      NULL);
  slidingwindow_add_plugins(sw2,
        make_swpercentile2(percentile,
            (GCompareFunc) bintree3cmp_int32,
            (ListenerFunc) _on_percentile_2_calced,
            &perc2_result,
            (SWExtractorFunc) _extractor,
            (SWMeanCalcer) _mean_calcer,
            NULL),
        NULL);
  slidingwindow_add_plugins(sw3,
        make_swlinpercentile(percentile,
            (GCompareFunc) bintree3cmp_pointer_int32,
            (ListenerFunc) _on_percentile_2_calced,
            &perc3_result,
            (SWExtractorFunc) _extractor,
            (SWMeanCalcer) _mean_calcer),
        NULL);
  *performance1 = 0;
  *performance2 = 0;
  *performance3 = 0;
  for (i = 0; i < runs; ++i) {
    gint32* value = g_malloc(sizeof(gint32));
    *value = rand() % max_random_number;
    {
      GstClockTime start;
      start = gst_clock_get_time(clock);
      slidingwindow_add_data(sw1, value);
      *performance1 += gst_clock_get_time(clock) - start;
    }
    {
      GstClockTime start;
      start = gst_clock_get_time(clock);
      slidingwindow_add_data(sw2, value);
      *performance2 += gst_clock_get_time(clock) - start;
    }
    {
      GstClockTime start;
      start = gst_clock_get_time(clock);
      slidingwindow_add_data(sw3, value);
      *performance3 += gst_clock_get_time(clock) - start;
    }
  }
}

static void _sw_test_results(gint32 percentile, gint32 runs, gint32 max_random_number, gint sw_size) {
  gint32 perc1_result, perc2_result, perc3_result, i;
  SlidingWindow* sw1 = make_slidingwindow(sw_size, GST_SECOND);
  SlidingWindow* sw2 = make_slidingwindow(sw_size, GST_SECOND);
  SlidingWindow* sw3 = make_slidingwindow(sw_size, GST_SECOND);
  slidingwindow_add_plugins(sw1,
      make_swpercentile(percentile,
          bintree3cmp_int32,
          (ListenerFunc)
          _on_percentile_1_calced,
          &perc1_result),
      NULL);
  slidingwindow_add_plugins(sw2,
        make_swpercentile2(percentile,
            (GCompareFunc) bintree3cmp_int32,
            (ListenerFunc) _on_percentile_2_calced,
            &perc2_result,
            (SWExtractorFunc) _extractor,
            (SWMeanCalcer) _mean_calcer,
            NULL),
        NULL);
  slidingwindow_add_plugins(sw3,
        make_swlinpercentile(percentile,
            (GCompareFunc) bintree3cmp_pointer_int32,
            (ListenerFunc) _on_percentile_2_calced,
            &perc3_result,
            (SWExtractorFunc) _extractor,
            (SWMeanCalcer) _mean_calcer),
        NULL);
  for (i = 0; i < runs; ++i) {
    gint32* value = g_malloc(sizeof(gint32));
    *value = rand() % max_random_number;
    slidingwindow_add_data(sw1, value);
    slidingwindow_add_data(sw2, value);
    slidingwindow_add_data(sw3, value);
    g_print("perc1: %d | perc2: %d | perc3: %d\n", perc1_result, perc2_result, perc3_result);
  }
}

void swperctester_do(void) {
  GstClockTime performance1 = 0, performance2 = 0, performance3 = 0;
  gint32 percentile = 50;
  gint32 runs = 10;
  gint32 max_random_number = 10;
  gint32 sw_size = 10;
  srand(time(NULL));   // should only be called once
  _sw_test_performance(percentile, runs, max_random_number, sw_size, &performance1, &performance2, &performance3);

  DISABLE_LINE _sw_test_results(percentile, runs, max_random_number, sw_size);

  g_print("%lu | %lu | %lu\n",
      GST_TIME_AS_USECONDS(performance1),
      GST_TIME_AS_USECONDS(performance2),
      GST_TIME_AS_USECONDS(performance3));
}
