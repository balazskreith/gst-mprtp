#!/bin/bash
programname=$0

TEMPDIR="temp"
SCRIPTSDIR="scripts"
ACTDIR=$SCRIPTSDIR"/runs/snd"

#setup defaults
DURATION=350

SCRIPTFILE=$TEMPDIR"/sender.sh"

echo -n "./snd_pipeline "                                  > $SCRIPTFILE
echo -n "--source=FILE:foreman_cif.yuv:1:352:288:2:25/1 " >> $SCRIPTFILE

echo -n "--codec=VP8 "                                    >> $SCRIPTFILE
echo -n "--stat=triggered_stat:temp/snd_packets.csv:3 "   >> $SCRIPTFILE

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

TCPSND_1=$TEMPDIR"/tcpsnd_1.sh"
echo "$ACTDIR/tcpflows.sh 10.0.0.6 12345 5" > $TCPSND_1
chmod 777 $TCPSND_1

trap control_c SIGINT
#Lets Rock
$ACTDIR/tcpflows.sh 10.0.0.6 12345 5 &
$ACTDIR/tcpflows.sh 10.0.0.6 12346 5 &
./$SCRIPTFILE & 
sleep $DURATION

cleanup

