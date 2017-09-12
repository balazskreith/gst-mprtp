#!/bin/bash
programname=$0
LOGSDIR="temp"

TEST="rmcat1"
ALGORITHM="SCReAM"
#ALGORITHM="FRACTaL"
OWD=50
JITTER=0

LISTENER_IN="/tmp/statlistener.cmds"
LISTENER_OUT="/tmp/statslistener.out"

mkdir temp_batch
rm temp_batch/*
rm triggered_stat

if [ -z "$1" ]
then
  ALGORITHM="FRACTaL"
else
  ALGORITHM=$1
fi

if [ -z "$2" ]
then
  OWD="50"
else
  OWD=$2
fi


COUNTER=0
if [ -z "$3" ]
then
  END=11
else
  END=$3
fi

SNDFILE="temp_batch/snd.sh"
echo "./scripts/runs/snd/rmcat1.sh $ALGORITHM" > $SNDFILE
chmod 777 $SNDFILE

RCVFILE="temp_batch/rcv.sh"
echo "./scripts/runs/rcv/rmcat1.sh $ALGORITHM" > $RCVFILE
chmod 777 $RCVFILE


cleanup()
{
  echo "DONE" >> $LISTENER_IN
  sleep 1
  sudo pkill statlistener
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

alg=${ALGORITHM,,}

echo "increase the number of datagrams for unix sockets"
sudo sysctl net.core.wmem_max=8192

rm $LISTENER_IN

while [  $COUNTER -lt $END ]; do
    echo "The counter is $COUNTER"
	./statlistener temp $LISTENER_IN &
	echo "ADD snd_packets.csv rcv_packets.csv ply_packets.csv" > $LISTENER_IN

	sudo ip netns exec ns_rcv $RCVFILE &
	sleep 2
	sudo ip netns exec ns_snd $SNDFILE &
	sleep 0.2
	./scripts/runs/rmcat1.sh $OWD $alg &

	INCREASE=1
	sleep 150
    echo "VALIDATE /tmp/statlistener.valid" >> $LISTENER_IN
	INCREASE=$(cat << /tmp/statlistener.valid)
	echo "DONE" >> $LISTENER_IN
	if [ $INCREASE -eq 0 ]
	then
	  echo "Increase is 0"
	fi
	cleanup

	#vqmt produced.yuv consumed.yuv 288 352 2000 1 temp/vqmt PSNR

	alg=${ALGORITHM,,}

	TARGET="temp_batch/"$alg"_"$COUNTER"_"$OWD"ms_"$JITTER"ms"
	echo $TARGET
	mkdir $TARGET
	cp temp/* $TARGET


    let COUNTER=COUNTER+$INCREASE
done
