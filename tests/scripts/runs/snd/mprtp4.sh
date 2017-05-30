#!/bin/bash
programname=$0

TEMPDIR="temp"
SCRIPTSDIR="scripts"
ACTDIR=$SCRIPTSDIR"/runs/snd"

#setup defaults
DURATION=200

SCRIPTFILE=$TEMPDIR"/sender.sh"

echo -n "./snd_pipeline "                                  > $SCRIPTFILE
echo -n "--source=FILE:foreman_cif.yuv:1:352:288:2:25/1 " >> $SCRIPTFILE

echo -n "--codec=VP8 "                                    >> $SCRIPTFILE
echo -n "--stat=triggered_stat:temp/snd_packets_1.csv:3 "   >> $SCRIPTFILE

echo -n "--sender=MPRTP:2:1:10.0.0.6:5000:2:10.0.1.6:5002 " >> $SCRIPTFILE
echo -n "--scheduler=MPRTPFRACTAL:MPRTP:2:1:5001:2:5003 "   >> $SCRIPTFILE

SCRIPTFILE2=$TEMPDIR"/sender_2.sh"

echo -n "./snd_pipeline "                                  > $SCRIPTFILE2
echo -n "--source=FILE:foreman_cif.yuv:1:352:288:2:25/1 " >> $SCRIPTFILE2

echo -n "--codec=VP8 "                                    >> $SCRIPTFILE2
echo -n "--stat=triggered_stat:temp/snd_packets_2.csv:3 "   >> $SCRIPTFILE2

echo -n "--sender=MPRTP:2:1:10.0.0.6:5004:2:10.0.1.6:5006 " >> $SCRIPTFILE2
echo -n "--scheduler=MPRTPFRACTAL:MPRTP:2:1:5005:2:5007 "   >> $SCRIPTFILE2

    
chmod 777 $SCRIPTFILE
chmod 777 $SCRIPTFILE2

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
sleep 20
./$SCRIPTFILE2 & 
sleep $DURATION

cleanup

