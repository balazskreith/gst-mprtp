#!/bin/bash
programname=$0

TEMPDIR="temp"
SCRIPTSDIR="scripts"
ACTDIR=$SCRIPTSDIR"/runs/snd"

#setup defaults
DURATION=150

SCRIPTFILE=$TEMPDIR"/sender.sh"

echo -n "./snd_pipeline "                                  > $SCRIPTFILE
echo -n "--source=FILE:foreman_cif.yuv:1:352:288:2:25/1 " >> $SCRIPTFILE
echo -n "--sourcesink=FILE:produced.yuv "                 >> $SCRIPTFILE

#echo -n "--codec=VP8:1:128 "                              >> $SCRIPTFILE
echo -n "--codec=VP8 "                                    >> $SCRIPTFILE
echo -n "--stat=100:1000:1:triggered_stat "               >> $SCRIPTFILE
#echo -n "--statlogsink=FILE:temp/snd_statlogs.csv "       >> $SCRIPTFILE
#echo -n "--packetlogsink=FILE:temp/snd_packetlogs.csv "   >> $SCRIPTFILE

echo -n "--statlogsink=MULTIFILE:2:1:temp/snd_statlogs_1.csv:2:temp/snd_statlogs_2.csv "       >> $SCRIPTFILE
echo -n "--packetlogsink=MULTIFILE:2:1:temp/snd_packetlogs_1.csv:2:temp/snd_packetlogs_2.csv "   >> $SCRIPTFILE

echo -n "--sender=MPRTP:2:1:10.0.0.6:5000:2:10.0.1.6:5002 " >> $SCRIPTFILE
echo -n "--scheduler=MPRTPFRACTAL:MPRTP:2:1:5001:2:5003 "   >> $SCRIPTFILE

    
chmod 777 $SCRIPTFILE

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
./$SCRIPTFILE & 
sleep $DURATION

cleanup

