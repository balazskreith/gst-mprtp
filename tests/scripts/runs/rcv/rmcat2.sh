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

echo -n "./rcv_pipeline "                                   > $SCRIPTFILE
#echo -n "--sink=FILE:consumed.yuv "                        >> $SCRIPTFILE
#echo -n "--sink=AUTOVIDEO "                                >> $SCRIPTFILE
echo -n "--sink=FAKESINK "                                 >> $SCRIPTFILE

echo -n "--codec=VP8 "                                     >> $SCRIPTFILE
echo -n "--stat=triggered_stat:rcv_logs_1.csv:0 "          >> $SCRIPTFILE
echo -n "--plystat=triggered_stat:ply_logs_1.csv:0 "       >> $SCRIPTFILE

echo $CC" is used as congestion control for receiver 1"
if [ $CC = $SCREAM ] 
then
	echo -n "--receiver=RTP:5000 "                             >> $SCRIPTFILE
	echo -n "--playouter=SCREAM:RTP:10.0.0.1:5001 "            >> $SCRIPTFILE
elif [ $CC = $FRACTAL ] 
then 
	echo -n "--playouter=MPRTPFRACTAL:MPRTP:1:1:10.0.0.1:5001 " >> $SCRIPTFILE
	echo -n "--receiver=MPRTP:1:1:5000 "                        >> $SCRIPTFILE
fi

chmod 777 $SCRIPTFILE

#------------------------------------------------------------------------

SCRIPTFILE2=$TEMPDIR"/receiver2.sh"

echo -n "./rcv_pipeline "                                   > $SCRIPTFILE2
#echo -n "--sink=FILE:consumed.yuv "                        >> $SCRIPTFILE2
#echo -n "--sink=AUTOVIDEO "                                >> $SCRIPTFILE2
echo -n "--sink=FAKESINK "                                 >> $SCRIPTFILE2

echo -n "--codec=VP8 "                                      >> $SCRIPTFILE2
echo -n "--stat=triggered_stat:rcv_logs_2.csv:0 "           >> $SCRIPTFILE2
echo -n "--plystat=triggered_stat:ply_logs_2.csv:0 "        >> $SCRIPTFILE2

echo $CC" is used as congestion control for receiver 2"
if [ $CC = $SCREAM ]
then
	echo -n "--receiver=RTP:5002 "                             >> $SCRIPTFILE2
	echo -n "--playouter=SCREAM:RTP:10.0.0.1:5003 "            >> $SCRIPTFILE2
elif [ $CC = $FRACTAL ] 
then 
	echo -n "--playouter=MPRTPFRACTAL:MPRTP:1:1:10.0.0.1:5003 " >> $SCRIPTFILE2
	echo -n "--receiver=MPRTP:1:1:5002 "                        >> $SCRIPTFILE2
fi

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

./$SCRIPTFILE  & 
./$SCRIPTFILE2 &
 
sleep $DURATION

cleanup

