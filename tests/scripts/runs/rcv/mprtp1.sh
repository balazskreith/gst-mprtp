#!/bin/bash
programname=$0

TEMPDIR="temp"
SCRIPTSDIR="scripts"
ACTDIR=$SCRIPTSDIR"/runs/snd"

SCREAM="SCReAM"
FRACTAL="FRACTaL"

rm $TEMPDIR/*
rm triggered_stat

#setup defaults
DURATION=150

SCRIPTFILE=$TEMPDIR"/receiver.sh"

echo -n "./rcv_pipeline "                                         > $SCRIPTFILE
echo -n "--sink=FILE:consumed.yuv "                              >> $SCRIPTFILE

echo -n "--codec=VP8 "                                           >> $SCRIPTFILE
echo -n "--stat=100:1000:1:triggered_stat "                      >> $SCRIPTFILE
#echo -n "--statlogsink=FILE:temp/rcv_statlogs.csv "              >> $SCRIPTFILE
#echo -n "--packetlogsink=FILE:temp/rcv_packetlogs.csv "          >> $SCRIPTFILE

echo -n "--statlogsink=MULTIFILE:2:1:temp/rcv_statlogs_1.csv:2:temp/rcv_statlogs_2.csv "       >> $SCRIPTFILE
echo -n "--packetlogsink=MULTIFILE:2:1:temp/rcv_packetlogs_1.csv:2:temp/rcv_packetlogs_2.csv "   >> $SCRIPTFILE

echo -n "--receiver=MPRTP:2:1:5000:2:5002 "                                >> $SCRIPTFILE
echo -n "--playouter=MPRTPFRACTAL:MPRTP:2:1:10.0.0.1:5001:2:10.0.1.1:5003 "  >> $SCRIPTFILE

chmod 777 $SCRIPTFILE

cleanup()
{
  pkill rcv_pipeline
}
 
control_c()
# run if user hits control-c
{
  echo -en "\n*** Program is terminated ***\n"
  cleanup
  exit $?
}

trap control_c SIGINT
#Lets Rock
./$SCRIPTFILE & 
sleep $DURATION

cleanup

