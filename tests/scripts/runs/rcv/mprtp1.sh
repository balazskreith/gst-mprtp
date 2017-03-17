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
echo -n "--stat=triggered_stat:temp/rcv_packets.csv:3 "                  >> $SCRIPTFILE
echo -n "--plystat=triggered_stat:temp/ply_packets.csv:3 "               >> $SCRIPTFILE

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

