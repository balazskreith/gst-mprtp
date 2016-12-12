#!/bin/bash
programname=$0
LOGSDIR="temp"

TEST="rmcat1"
# ALGORITHM="SCReAM"
ALGORITHM="FRACTaL"
OWD=50
JITTER=0

mkdir temp_batch
rm temp_batch/*
rm triggered_stat

SNDFILE="temp_batch/snd.sh"
echo "./scripts/runs/snd/rmcat1.sh $ALGORITHM" > $SNDFILE
chmod 777 $SNDFILE

RCVFILE="temp_batch/rcv.sh"
echo "./scripts/runs/rcv/rmcat1.sh $ALGORITHM" > $RCVFILE
chmod 777 $RCVFILE


COUNTER=0
if [ -z "$3" ]
then
  END=11
else
  END=$3
fi

while [  $COUNTER -lt $END ]; do
    echo "The counter is $COUNTER"

	sudo ip netns exec ns_rcv $RCVFILE &
	sleep 2
	sudo ip netns exec ns_snd $SNDFILE &
	sleep 2
	./scripts/runs/rmcat1.sh $OWD &
	sleep 10

	INCREASE=1

	#Validation part 1.
	for FILE in snd_packetlogs.csv rcv_packetlogs.csv snd_statlogs.csv rcv_statlogs.csv
	do
   		if [ ! -f $LOGSDIR"/"$FILE ]; then
    		INCREASE=0
    		echo $FILE" not found"
		fi
	done

	if [ $INCREASE -eq 0 ]
	then
	  sudo pkill snd_pipeline
	  sudo pkill rcv_pipeline
	  sudo pkill ntrt
	  continue
	fi

	echo "Validation: Necessary logfile exists."

	sleep 50

	#Validation Part 2
	for FILE in snd_packetlogs.csv rcv_packetlogs.csv
	do
		minimumsize=9000
		actualsize=$(wc -c <"$LOGSDIR/$FILE")
		if [ ! $actualsize -ge $minimumsize ]; then
		    echo "-----$FILE SIZE IS UNDER $minimumsize BYTES-----"
		    INCREASE=0
    		echo $FILE" not found"
		fi
	done

	if [ $INCREASE -eq 0 ]
	then
	  sudo pkill snd_pipeline
	  sudo pkill rcv_pipeline
	  sudo pkill ntrt
	  continue
	fi

	echo "Validation: Logfiles size seems ok."

	sleep 150

	sudo pkill snd_pipeline
	sudo pkill rcv_pipeline
	sudo pkill ntrt

	#vqmt produced.yuv consumed.yuv 288 352 2000 1 temp/vqmt PSNR

	alg=${ALGORITHM,,}

	TARGET="temp_batch/"$alg"_"$COUNTER"_"$OWD"ms_"$JITTER"ms"
	mkdir $TARGET
	cp temp/* $TARGET


    let COUNTER=COUNTER+$INCREASE
done
