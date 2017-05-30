#!/bin/bash
programname=$0
LOGSDIR="temp"

TEST="mprtp5"
# ALGORITHM="SCReAM"
ALGORITHM="FRACTaL"
OWD=50
JITTER=0

mkdir temp_batch
rm temp_batch/*
rm triggered_stat

SNDFILE="temp_batch/snd.sh"
echo "./scripts/runs/snd/mprtp5.sh $ALGORITHM" > $SNDFILE
chmod 777 $SNDFILE

RCVFILE="temp_batch/rcv.sh"
echo "./scripts/runs/rcv/mprtp5.sh $ALGORITHM" > $RCVFILE
chmod 777 $RCVFILE

cleanup()
{
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

while [  $COUNTER -lt $END ]; do
    echo "The counter is $COUNTER"

	sudo ip netns exec ns_rcv $RCVFILE &
	sleep 2
	sudo ip netns exec ns_snd $SNDFILE &
	sleep 2
	./scripts/runs/mprtp5.sh $OWD &
	sleep 30

	INCREASE=1

	#Validation part 1.
	for FILE in snd_packets_1.csv rcv_packets_1.csv
	do
   		if [ ! -f $LOGSDIR"/"$FILE ]; then
    		INCREASE=0
    		echo $FILE" not found"
		fi
	done

	if [ $INCREASE -eq 0 ]
	then
	  cleanup
	  sleep 350
	  continue
	fi

	echo "Validation: Necessary logfile exists."

	sleep 50

	#Validation Part 2
	for FILE in snd_packets_1.csv rcv_packets_1.csv
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
	  cleanup
	  sleep 350
	  continue
	fi

	echo "Validation: Logfiles size seems ok."

	sleep 250

	cleanup

	#vqmt produced.yuv consumed.yuv 288 352 2000 1 temp/vqmt PSNR

	alg=${ALGORITHM,,}

	TARGET="temp_batch/"$alg"_"$COUNTER"_"$OWD"ms_"$JITTER"ms"
	mkdir $TARGET
	cp temp/* $TARGET

    let COUNTER=COUNTER+$INCREASE
done
