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
echo -n "--source=FILE:foreman_cif.yuv:1:352:288:2:25/1 " >> $SCRIPTFILE
echo -n "--sourcesink=FILE:produced.yuv "                 >> $SCRIPTFILE

echo -n "--codec=VP8 "                                    >> $SCRIPTFILE
echo -n "--stat=triggered_stat:temp/snd_packets.csv:0 "   >> $SCRIPTFILE

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

cleanup()
{
  pkill iperf
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

let "r=$RANDOM%30"
iperf -c 10.0.0.6 -p 12345 -t $r &
sleep 5
./$SCRIPTFILE &
for (( c=1; c<=($RANDOM % 10) + 10; c++ ))
do  
	let "r=$RANDOM%30"
	let "s=$RANDOM%20 + 1"
	iperf -c 10.0.0.6 -p 12345 -t $r &
	sleep $s
done
 
sleep 320

cleanup

