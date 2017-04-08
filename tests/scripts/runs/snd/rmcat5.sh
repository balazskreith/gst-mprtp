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
DURATION=100

SCRIPTFILE=$TEMPDIR"/sender.sh"

echo -n "./snd_pipeline "                                  > $SCRIPTFILE
echo -n "--source=FILE:foreman_cif.yuv:1:352:288:2:25/1 " >> $SCRIPTFILE
#echo -n "--sourcesink=FILE:produced.yuv "                 >> $SCRIPTFILE

echo -n "--codec=VP8 "                                    >> $SCRIPTFILE
echo -n "--stat=triggered_stat:temp/snd_packets_1.csv:0 " >> $SCRIPTFILE

echo $CC" is used as congestion control for sender 1"
if [ $CC = $SCREAM ]
then
	echo -n "--sender=RTP:10.0.0.6:5000 "                     >> $SCRIPTFILE
	echo -n "--scheduler=SCREAM:RTP:5001 "                    >> $SCRIPTFILE
elif [ $CC = $FRACTAL ] 
then 
	echo -n "--sender=MPRTP:1:1:10.0.0.6:5000 "                >> $SCRIPTFILE
	echo -n "--scheduler=MPRTPFRACTAL:MPRTP:1:1:5001 "         >> $SCRIPTFILE
fi


chmod 777 $SCRIPTFILE


#------------------------------------------------------------------------

SCRIPTFILE2=$TEMPDIR"/sender2.sh"

echo -n "./snd_pipeline "                                  > $SCRIPTFILE2
echo -n "--source=FILE:foreman_cif.yuv:1:352:288:2:25/1 "  >> $SCRIPTFILE2
#echo -n "--sourcesink=FILE:produced.yuv "                 >> $SCRIPTFILE2

echo -n "--extradelay=50 "                                 >> $SCRIPTFILE2

echo -n "--codec=VP8 "                                    >> $SCRIPTFILE2
echo -n "--stat=triggered_stat:temp/snd_packets_2.csv:0 " >> $SCRIPTFILE2

echo $CC" is used as congestion control for sender 2"
if [ $CC = $SCREAM ]
then
	echo -n "--sender=RTP:10.0.0.6:5002 "                     >> $SCRIPTFILE2
	echo -n "--scheduler=SCREAM:RTP:5003 "                    >> $SCRIPTFILE2
elif [ $CC = $FRACTAL ] 
then 
	echo -n "--sender=MPRTP:1:1:10.0.0.6:5002 "                >> $SCRIPTFILE2
	echo -n "--scheduler=MPRTPFRACTAL:MPRTP:1:1:5003 "         >> $SCRIPTFILE2
fi


chmod 777 $SCRIPTFILE2

#------------------------------------------------------------------------

SCRIPTFILE3=$TEMPDIR"/sender3.sh"

echo -n "./snd_pipeline "                                  > $SCRIPTFILE3
echo -n "--source=FILE:foreman_cif.yuv:1:352:288:2:25/1 " >> $SCRIPTFILE3
#echo -n "--sourcesink=FILE:produced.yuv "                 >> $SCRIPTFILE3

echo -n "--extradelay=150 "                                 >> $SCRIPTFILE3

echo -n "--codec=VP8 "                                    >> $SCRIPTFILE3
echo -n "--stat=triggered_stat:temp/snd_packets_3.csv:0 " >> $SCRIPTFILE3

echo $CC" is used as congestion control for sender 3"
if [ $CC = $SCREAM ]
then
	echo -n "--sender=RTP:10.0.0.6:5004 "                     >> $SCRIPTFILE3
	echo -n "--scheduler=SCREAM:RTP:5005 "                    >> $SCRIPTFILE3
elif [ $CC = $FRACTAL ] 
then 
	echo -n "--sender=MPRTP:1:1:10.0.0.6:5004 "                >> $SCRIPTFILE3
	echo -n "--scheduler=MPRTPFRACTAL:MPRTP:1:1:5005 "         >> $SCRIPTFILE3
fi


chmod 777 $SCRIPTFILE3



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
echo "Start $SCRIPTFILE"
./$SCRIPTFILE  & 
sleep 10
echo "Start $SCRIPTFILE2"
./$SCRIPTFILE2 &
sleep 10
echo "Start $SCRIPTFILE3"
./$SCRIPTFILE3 &

sleep 300

cleanup

