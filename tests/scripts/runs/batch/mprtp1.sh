#!/bin/bash
programname=$0
LOGSDIR="temp"

TEST="mprtp1"
# ALGORITHM="SCReAM"
ALGORITHM="FRACTaL"
OWD=50
JITTER=0

LISTENER_IN="/tmp/statlistener.cmds"
LISTENER_OUT="/tmp/statslistener.out"

mkdir temp_batch
rm temp_batch/*
rm triggered_stat

SNDFILE="temp_batch/snd.sh"
echo "./scripts/runs/snd/mprtp1.sh $ALGORITHM" > $SNDFILE
chmod 777 $SNDFILE

RCVFILE="temp_batch/rcv.sh"
echo "./scripts/runs/rcv/mprtp1.sh $ALGORITHM" > $RCVFILE
chmod 777 $RCVFILE

cleanup()
{
  echo "DONE" >> $LISTENER_IN
  sleep 1
  sudo pkill snd_pipeline
  sudo pkill rcv_pipeline
  sudo pkill bcex
  sudo pkill bwcsv
  sudo pkill sleep
}
 
control_c()
{
  echo -en "\n*** Program is terminated ***\n"
  cleanup
  exit $?
}

trap control_c SIGINT


COUNTER=0
if [ -z "$3" ]
then
  END=1
else
  END=$3
fi

rm $LISTENER_IN

while [  $COUNTER -lt $END ]; do
    echo "The counter is $COUNTER"

	./statlistener temp $LISTENER_IN &
	echo "ADD snd_packets.csv rcv_packets.csv ply_packets.csv" > $LISTENER_IN

	sudo ip netns exec ns_rcv $RCVFILE &
	sleep 2
	sudo ip netns exec ns_snd $SNDFILE &
	sleep 2
	./scripts/runs/mprtp1.sh $OWD &
	
	sleep 150
	echo "VALIDATE /tmp/statlistener.valid" >> $LISTENER_IN
	INCREASE=$(cat << /tmp/statlistener.valid)
	echo "DONE" >> $LISTENER_IN
	if [ $INCREASE -eq 0 ]
	then
	  cleanup
	  continue
	fi
	cleanup

	#vqmt produced.yuv consumed.yuv 288 352 2000 1 temp/vqmt PSNR

	alg=${ALGORITHM,,}

	TARGET="temp_batch/"$alg"_"$COUNTER"_"$OWD"ms_"$JITTER"ms"
	mkdir $TARGET
	cp temp/* $TARGET


    let COUNTER=COUNTER+$INCREASE
done
