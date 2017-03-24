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


#setup defaults
DURATION=150

SCRIPTFILE=$TEMPDIR"/sender.sh"

echo -n "./snd_pipeline "                                  > $SCRIPTFILE
echo -n "--source=FILE:foreman_cif.yuv:1:352:288:2:25/1 "  >> $SCRIPTFILE
#echo -n "--sourcesink=FILE:produced.yuv "                 >> $SCRIPTFILE

echo -n "--codec=VP8 "                                    >> $SCRIPTFILE
echo -n "--stat=triggered_stat:temp/snd_packets_1.csv:0 " >> $SCRIPTFILE

echo $CC" is used as congestion control for sender 1"
if [ $CC = $SCREAM ]
then
	echo -n "--sender=RTP:10.0.0.6:5100 "                     >> $SCRIPTFILE
	echo -n "--scheduler=SCREAM:RTP:5101 "                    >> $SCRIPTFILE
elif [ $CC = $FRACTAL ] 
then 
	echo -n "--sender=MPRTP:1:1:10.0.0.6:5100 "                >> $SCRIPTFILE
	echo -n "--scheduler=MPRTPFRACTAL:MPRTP:1:1:5101 "         >> $SCRIPTFILE
fi


chmod 777 $SCRIPTFILE


#------------------------------------------------------------------------

SCRIPTFILE2=$TEMPDIR"/receiver2.sh"

echo -n "./rcv_pipeline "                                   > $SCRIPTFILE2
#echo -n "--sink=FILE:consumed.yuv "                        >> $SCRIPTFILE2
#echo -n "--sink=AUTOVIDEO "                                >> $SCRIPTFILE2
echo -n "--sink=FAKESINK "                                 >> $SCRIPTFILE2

echo -n "--codec=VP8 "                                      >> $SCRIPTFILE2
echo -n "--stat=triggered_stat:temp/rcv_packets_2.csv:0 "           >> $SCRIPTFILE2
echo -n "--plystat=triggered_stat:temp/ply_packets_2.csv:0 "       >> $SCRIPTFILE2

echo $CC" is used as congestion control for receiver 2"
if [ $CC = $SCREAM ]
then
	echo -n "--receiver=RTP:5102 "                             >> $SCRIPTFILE2
	echo -n "--playouter=SCREAM:RTP:10.0.0.6:5103 "            >> $SCRIPTFILE2
elif [ $CC = $FRACTAL ] 
then 
	echo -n "--playouter=MPRTPFRACTAL:MPRTP:1:1:10.0.0.6:5103 " >> $SCRIPTFILE2
	echo -n "--receiver=MPRTP:1:1:5102 "                        >> $SCRIPTFILE2
fi

chmod 777 $SCRIPTFILE2

cleanup()
{
  pkill snd_pipeline
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

