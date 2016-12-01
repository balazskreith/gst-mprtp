#!/bin/bash
programname=$0

TEMPDIR="temp"
SCRIPTSDIR="scripts"
ACTDIR=$SCRIPTSDIR"/runs/snd"

SCREAM="SCReAM"
FRACTAL="FRACTaL"

if [ -z "$1" ] 
then
  CC=$SCREAM
  CC=$FRACTAL
else 
  CC=$1
fi


rm $TEMPDIR/*
rm triggered_stat

#setup defaults
DURATION=150

SCRIPTFILE=$TEMPDIR"/receiver.sh"

echo -n "./rcv_pipeline "                                         > $SCRIPTFILE
echo -n "--sink=FILE:consumed.yuv "                              >> $SCRIPTFILE

echo -n "--codec=VP8 "                                           >> $SCRIPTFILE
echo -n "--stat=100:1000:1:triggered_stat "                      >> $SCRIPTFILE
echo -n "--statlogsink=FILE:temp/rcv_statlogs.csv "              >> $SCRIPTFILE
echo -n "--packetlogsink=FILE:temp/rcv_packetlogs.csv "          >> $SCRIPTFILE

echo $CC" is used as congestion control for receiver 1"
if [ $CC = $SCREAM ]
then
	echo -n "--receiver=RTP:5000 "                               >> $SCRIPTFILE
	echo -n "--playouter=SCREAM:RTP:10.0.0.1:5001 "              >> $SCRIPTFILE
elif [ $CC = $FRACTAL ] 
then 
	echo -n "--receiver=MPRTP:1:1:5000 "                         >> $SCRIPTFILE
	echo -n "--playouter=MPRTPFRACTAL:MPRTP:1:1:10.0.0.1:5001 "  >> $SCRIPTFILE
fi

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
iperf -s -p 12345 &
./$SCRIPTFILE & 
sleep 140

cleanup

