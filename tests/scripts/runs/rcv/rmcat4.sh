#!/bin/bash
programname=$0

TEMPDIR="temp"
SCRIPTSDIR="scripts"
ACTDIR=$SCRIPTSDIR"/runs/snd"

SCREAM="SCReAM"
FRACTAL="FRACTaL"
CC=$SCREAM
#CC=$FRACTAL


rm $TEMPDIR/*
rm triggered_stat

#setup defaults

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

echo -n "--codec=VP8 "                                     >> $SCRIPTFILE2
echo -n "--stat=100:1000:1:triggered_stat "                >> $SCRIPTFILE2
echo -n "--statlogsink=FILE:temp/rcv_statlogs2.csv "        >> $SCRIPTFILE2
echo -n "--packetlogsink=FILE:temp/rcv_packetlogs2.csv "    >> $SCRIPTFILE2


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

#------------------------------------------------------------------------

SCRIPTFILE3=$TEMPDIR"/receiver3.sh"

echo -n "./rcv_pipeline "                                   > $SCRIPTFILE3
#echo -n "--sink=FILE:consumed.yuv "                        >> $SCRIPTFILE3
#echo -n "--sink=AUTOVIDEO "                                >> $SCRIPTFILE3
echo -n "--sink=FAKESINK "                                 >> $SCRIPTFILE3

echo -n "--codec=VP8 "                                     >> $SCRIPTFILE3
echo -n "--stat=100:1000:1:triggered_stat "                >> $SCRIPTFILE3
echo -n "--statlogsink=FILE:temp/rcv_statlogs3.csv "        >> $SCRIPTFILE3
echo -n "--packetlogsink=FILE:temp/rcv_packetlogs3.csv "    >> $SCRIPTFILE3


echo $CC" is used as congestion control for receiver 3"
if [ $CC = $SCREAM ] 
then
	echo -n "--receiver=RTP:5004 "                             >> $SCRIPTFILE3
	echo -n "--playouter=SCREAM:RTP:10.0.0.1:5005 "            >> $SCRIPTFILE3
elif [ $CC = $FRACTAL ] 
then 
	echo -n "--playouter=MPRTPFRACTAL:MPRTP:1:1:10.0.0.1:5005 " >> $SCRIPTFILE3
	echo -n "--receiver=MPRTP:1:1:5004 "                        >> $SCRIPTFILE3
fi

chmod 777 $SCRIPTFILE3



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
sleep 15
./$SCRIPTFILE2 &
sleep 20
./$SCRIPTFILE3 &
 
sleep 150

cleanup

