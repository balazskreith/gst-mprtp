#!/bin/bash
programname=$0

TEMPDIR="temp"
SCRIPTSDIR="scripts"
ACTDIR=$SCRIPTSDIR"/runs/snd"

SCREAM="SCReAM"
FRACTAL="FRACTaL"
#CC=$SCREAM
CC=$FRACTAL


#setup defaults
DURATION=100

SCRIPTFILE=$TEMPDIR"/sender.sh"

echo -n "./snd_pipeline "                                  > $SCRIPTFILE
echo -n "--source=FILE:foreman_cif.yuv:1:352:288:2:25/1 " >> $SCRIPTFILE
#echo -n "--sourcesink=FILE:produced.yuv "                 >> $SCRIPTFILE

echo -n "--codec=VP8 "                                    >> $SCRIPTFILE
echo -n "--stat=100:1000:1:triggered_stat "               >> $SCRIPTFILE
echo -n "--statlogsink=FILE:temp/snd_statlogs.csv "       >> $SCRIPTFILE
echo -n "--packetlogsink=FILE:temp/snd_packetlogs.csv "   >> $SCRIPTFILE

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

echo -n "--extradelay=15 "                                 >> $SCRIPTFILE2

echo -n "--codec=VP8 "                                    >> $SCRIPTFILE2
echo -n "--stat=100:1000:1:triggered_stat "               >> $SCRIPTFILE2
echo -n "--statlogsink=FILE:temp/snd_statlogs2.csv "       >> $SCRIPTFILE2
echo -n "--packetlogsink=FILE:temp/snd_packetlogs2.csv "   >> $SCRIPTFILE2

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

echo -n "--extradelay=40 "                                 >> $SCRIPTFILE3

echo -n "--codec=VP8 "                                    >> $SCRIPTFILE3
echo -n "--stat=100:1000:1:triggered_stat "               >> $SCRIPTFILE3
echo -n "--statlogsink=FILE:temp/snd_statlogs3.csv "       >> $SCRIPTFILE3
echo -n "--packetlogsink=FILE:temp/snd_packetlogs3.csv "   >> $SCRIPTFILE3

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



#------------------------------------------------------------------------

SCRIPTFILE4=$TEMPDIR"/sender4.sh"

echo -n "./snd_pipeline "                                  > $SCRIPTFILE4
echo -n "--source=FILE:foreman_cif.yuv:1:352:288:2:25/1 " >> $SCRIPTFILE4
#echo -n "--sourcesink=FILE:produced.yuv "                 >> $SCRIPTFILE4

echo -n "--extradelay=90 "                                 >> $SCRIPTFILE4

echo -n "--codec=VP8 "                                    >> $SCRIPTFILE4
echo -n "--stat=100:1000:1:triggered_stat "               >> $SCRIPTFILE4
echo -n "--statlogsink=FILE:temp/snd_statlogs4.csv "       >> $SCRIPTFILE4
echo -n "--packetlogsink=FILE:temp/snd_packetlogs4.csv "   >> $SCRIPTFILE4

echo $CC" is used as congestion control for sender 4"
if [ $CC = $SCREAM ]
then
	echo -n "--sender=RTP:10.0.0.6:5006 "                     >> $SCRIPTFILE4
	echo -n "--scheduler=SCREAM:RTP:5007 "                    >> $SCRIPTFILE4
elif [ $CC = $FRACTAL ] 
then 
	echo -n "--sender=MPRTP:1:1:10.0.0.6:5006 "                >> $SCRIPTFILE4
	echo -n "--scheduler=MPRTPFRACTAL:MPRTP:1:1:5007 "         >> $SCRIPTFILE4
fi


chmod 777 $SCRIPTFILE4


#------------------------------------------------------------------------

SCRIPTFILE5=$TEMPDIR"/sender5.sh"

echo -n "./snd_pipeline "                                  > $SCRIPTFILE5
echo -n "--source=FILE:foreman_cif.yuv:1:352:288:2:25/1 " >> $SCRIPTFILE5
#echo -n "--sourcesink=FILE:produced.yuv "                 >> $SCRIPTFILE5

echo -n "--extradelay=140 "                                 >> $SCRIPTFILE5

echo -n "--codec=VP8 "                                    >> $SCRIPTFILE5
echo -n "--stat=100:1000:1:triggered_stat "               >> $SCRIPTFILE5
echo -n "--statlogsink=FILE:temp/snd_statlogs5.csv "       >> $SCRIPTFILE5
echo -n "--packetlogsink=FILE:temp/snd_packetlogs5.csv "   >> $SCRIPTFILE5

echo $CC" is used as congestion control for sender 5"
if [ $CC = $SCREAM ]
then
	echo -n "--sender=RTP:10.0.0.6:5008 "                     >> $SCRIPTFILE5
	echo -n "--scheduler=SCREAM:RTP:5009 "                    >> $SCRIPTFILE5
elif [ $CC = $FRACTAL ] 
then 
	echo -n "--sender=MPRTP:1:1:10.0.0.6:5008 "                >> $SCRIPTFILE5
	echo -n "--scheduler=MPRTPFRACTAL:MPRTP:1:1:5009 "         >> $SCRIPTFILE5
fi


chmod 777 $SCRIPTFILE5


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
sleep 10
echo "Start $SCRIPTFILE4"
./$SCRIPTFILE4 &
 sleep 10
echo "Start $SCRIPTFILE5"
./$SCRIPTFILE5 &

sleep 300

cleanup

