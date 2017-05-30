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
DURATION=350

SCRIPTFILE=$TEMPDIR"/receiver.sh"

echo -n "./rcv_pipeline "                                                > $SCRIPTFILE
echo -n "--sink=FAKESINK "                                               >> $SCRIPTFILE

echo -n "--codec=VP8 "                                                   >> $SCRIPTFILE
echo -n "--stat=triggered_stat:temp/rcv_packets_1.csv:3 "                  >> $SCRIPTFILE
echo -n "--plystat=triggered_stat:temp/ply_packets_1.csv:3 "               >> $SCRIPTFILE

echo -n "--receiver=MPRTP:2:1:5000:2:5002 "                                  >> $SCRIPTFILE
echo -n "--playouter=MPRTPFRACTAL:MPRTP:2:1:10.0.0.1:5001:2:10.0.1.1:5003 "  >> $SCRIPTFILE

chmod 777 $SCRIPTFILE
SCRIPTFILE2=$TEMPDIR"/receiver_2.sh"

echo -n "./rcv_pipeline "                                                > $SCRIPTFILE2
echo -n "--sink=FAKESINK "                                               >> $SCRIPTFILE2

echo -n "--codec=VP8 "                                                   >> $SCRIPTFILE2
echo -n "--stat=triggered_stat:temp/rcv_packets_2.csv:3 "                  >> $SCRIPTFILE2
echo -n "--plystat=triggered_stat:temp/ply_packets_2.csv:3 "               >> $SCRIPTFILE2

echo -n "--receiver=MPRTP:2:1:5004:2:5006 "                                  >> $SCRIPTFILE2
echo -n "--playouter=MPRTPFRACTAL:MPRTP:2:1:10.0.0.1:5005:2:10.0.1.1:5007 "  >> $SCRIPTFILE2

chmod 777 $SCRIPTFILE2

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
sleep 10
./$SCRIPTFILE2 & 
sleep $DURATION

cleanup

