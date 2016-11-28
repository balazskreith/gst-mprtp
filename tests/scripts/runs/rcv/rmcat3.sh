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
echo -n "--stat=100:1000:1:triggered_stat "                >> $SCRIPTFILE
echo -n "--statlogsink=FILE:temp/rcv_statlogs.csv "        >> $SCRIPTFILE
echo -n "--packetlogsink=FILE:temp/rcv_packetlogs.csv "    >> $SCRIPTFILE


echo $CC" is used as congestion control for receiver 1"
if [ $CC = $SCREAM ] 
then
	echo -n "--receiver=RTP:5100 "                             >> $SCRIPTFILE
	echo -n "--playouter=SCREAM:RTP:10.0.0.1:5101 "            >> $SCRIPTFILE
elif [ $CC = $FRACTAL ] 
then 
	echo -n "--playouter=MPRTPFRACTAL:MPRTP:1:1:10.0.0.1:5101 " >> $SCRIPTFILE
	echo -n "--receiver=MPRTP:1:1:5100 "                        >> $SCRIPTFILE
fi

chmod 777 $SCRIPTFILE

#------------------------------------------------------------------------

SCRIPTFILE2=$TEMPDIR"/sender2.sh"

echo -n "./snd_pipeline "                                  > $SCRIPTFILE2
echo -n "--source=FILE:foreman_cif.yuv:1:352:288:2:25/1 " >> $SCRIPTFILE2
#echo -n "--sourcesink=FILE:produced.yuv "                 >> $SCRIPTFILE2

echo -n "--codec=VP8 "                                    >> $SCRIPTFILE2
echo -n "--stat=100:1000:1:triggered_stat "               >> $SCRIPTFILE2
echo -n "--statlogsink=FILE:temp/snd_statlogs2.csv "      >> $SCRIPTFILE2
echo -n "--packetlogsink=FILE:temp/snd_packetlogs2.csv "  >> $SCRIPTFILE2

echo $CC" is used as congestion control for sender 2"
if [ $CC = $SCREAM ]
then
	echo -n "--sender=RTP:10.0.0.1:5102 "                     >> $SCRIPTFILE2
	echo -n "--scheduler=SCREAM:RTP:5103 "                    >> $SCRIPTFILE2
elif [ $CC = $FRACTAL ] 
then 
	echo -n "--sender=MPRTP:1:1:10.0.0.1:5102 "                >> $SCRIPTFILE2
	echo -n "--scheduler=MPRTPFRACTAL:MPRTP:1:1:5103 "         >> $SCRIPTFILE2
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

