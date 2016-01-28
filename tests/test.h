/*
 * test.h
 *
 *  Created on: Jan 26, 2016
 *      Author: balazs
 */

#ifndef TESTS_TEST_H_
#define TESTS_TEST_H_

#include <string.h>

typedef enum{
  NO_CONTROLLING               = 0,
  MANUAL_RATE_CONTROLLING      = 1,
  AUTO_RATE_AND_CC_CONTROLLING = 2,
}TestSuite;

typedef enum{
  TEST_SOURCE    = 0,
  FOREMAN_SOURCE = 1,
  VL2SRC         = 2,
}VideoSession;

typedef struct _TestParams{
  TestSuite    test_directive;
  VideoSession video_session;
  gboolean     subflow1_active;
  gboolean     subflow2_active;
  gboolean     subflow3_active;

  gboolean     other_variable_used_for_debugging_because_i_am_tired_to_recompile_it_every_time;
}TestParams;

static TestParams test_parameters_;
static gint32 profile;
static gint32 info;

static GOptionEntry entries[] =
{
    { "profile", 0, 0, G_OPTION_ARG_INT, &profile, "Profile", NULL },
    { "info", 0, 0, G_OPTION_ARG_NONE, &info, "Info", NULL },
  { NULL }
};


#define println(str) g_print(str"\n")
static void _print_info(void)
{
  println("####################### Test profiles #############################");
  println("#                                                                 #");
  println("# profile = 0b00|00|0|0|0                                         #");
  println("#              ^  ^ ^ ^ ^                                         #");
  println("#              |  | | | |0/1 - Deactivate/Activate subflow 1      #");
  println("#              |  | | |0/1 - Deactivate/Activate subflow 2        #");
  println("#              |  | |0/1 - Deactivate/Activate subflow 3          #");
  println("#              |  |0 - Test source, 1 - v4l2src, 2 - foreman seq  #");
  println("#              |0 - No rate control, 1 - random rate ctrl,        #");
  println("#              |2 - auto rate and cc control                      #");
  println("# Examples:                                                       #");
  println("# --profile=1 <- subflow 1, test source, no rate controller       #");
  println("# --profile=3 <- subflow 1 and 2, test source, no rate controller #");
  println("# --profile=67 <- sub 1 and 2, test source, rate and cc           #");
  println("###################################################################");
}


static void _setup_test_params(guint profile)
{
  memset(&test_parameters_, 0, sizeof(TestParams));
  if(profile == 0){
      profile = 1;
//      _print_info();
  }
  g_print("Selected test profile: %d, it setups the following:\n", profile);

  test_parameters_.subflow1_active = (profile & 1) ? TRUE : FALSE;
  g_print("%s subflow 1\n", test_parameters_.subflow1_active?"Active":"Deactive");
  test_parameters_.subflow2_active = (profile & 2) ? TRUE : FALSE;
  g_print("%s subflow 2\n", test_parameters_.subflow2_active?"Active":"Deactive");
  test_parameters_.subflow3_active = (profile & 4) ? TRUE : FALSE;
  g_print("%s subflow 3\n", test_parameters_.subflow3_active?"Active":"Deactive");

  test_parameters_.video_session =(VideoSession)((profile & 24)>>3);
  switch (test_parameters_.video_session) {
    case VL2SRC:
      g_print("Vl2 source is selected\n");
      break;
    case FOREMAN_SOURCE:
      g_print("Foreman sequence is selected\n");
      break;
    case TEST_SOURCE:
    default:
      g_print("Test video source is selected\n");
      break;
  }

  test_parameters_.test_directive = (TestSuite)((profile & 96)>>5);
  switch (test_parameters_.test_directive) {
      case MANUAL_RATE_CONTROLLING:
        g_print("Manual Rate controller is selected.\n");
        break;
      case AUTO_RATE_AND_CC_CONTROLLING:
        g_print("Automatic rate and congestion controller mode is selected.\n");
        break;
      default:
      case NO_CONTROLLING:
        g_print("No rate or flow controlling is enabled.\n");
        break;
    }

  test_parameters_.other_variable_used_for_debugging_because_i_am_tired_to_recompile_it_every_time = (profile & 128) > 0 ? TRUE : FALSE;
}



#endif /* TESTS_TEST_H_ */
