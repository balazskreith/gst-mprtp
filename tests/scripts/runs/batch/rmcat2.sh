#!/bin/bash
programname=$0
LOGSDIR="temp"
TEMPDIR="temp_batch"

TEST="rmcat2"
ALGORITHM="SCReAM"
# ALGORITHM="FRACTaL"
OWD=50
JITTER=0

mkdir $TEMPDIR
rm $TEMPDIR/*

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



if [ -z "$3" ]
then
  END=11
else
  END=$3
fi


SNDFILE="$TEMPDIR/snd.sh"
echo "./scripts/runs/snd/$TEST.sh $ALGORITHM" > $SNDFILE
chmod 777 $SNDFILE

RCVFILE="$TEMPDIR/rcv.sh"
echo "./scripts/runs/rcv/$TEST.sh $ALGORITHM" > $RCVFILE
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
while [  $COUNTER -lt $END ]; do
    echo "The counter is $COUNTER"

	sudo ip netns exec ns_rcv $RCVFILE &
	sleep 2
	sudo ip netns exec ns_snd $SNDFILE &
	sleep 0.2
	./scripts/runs/$TEST.sh $OWD &
	sleep 10

	INCREASE=1

	#Flow 1
	for FILE in snd_packets_1.csv rcv_packets_1.csv snd_packets_2.csv rcv_packets_2.csv
	do
   		if [ ! -f $LOGSDIR"/"$FILE ]; then
    		INCREASE=0
    		echo $FILE" not found"
		fi
	done

	if [ $INCREASE -eq 0 ]
	then
	  cleanup
	  continue
	fi

	sleep 50

	#Validation Part 2
	for FILE in snd_packets_1.csv rcv_packets_1.csv snd_packets_2.csv rcv_packets_2.csv
	do
		minimumsize=20000
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
	  continue
	fi

	sleep 150

	cleanup

	alg=${ALGORITHM,,}

	TARGET="$TEMPDIR/"$alg"_"$COUNTER"_"$OWD"ms_"$JITTER"ms"
	mkdir $TARGET
	cp temp/* $TARGET


    let COUNTER=COUNTER+$INCREASE
done
