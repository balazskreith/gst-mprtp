#!/bin/bash
programname=$0

NSSND="ns_snd"
NSRCV="ns_rcv"
SENDER="sender"
RECEIVER="receiver"
LOGSDIR="logs"
REPORTSDIR="reports"
SCRIPTSDIR="scripts2"
TEMPDIR=$SCRIPTSDIR"/temp"
EVALDIR=$SCRIPTSDIR"/evals"
CONFDIR=$SCRIPTSDIR"/configs"
RUNDIR=$SCRIPTSDIR"/runs"

rm -R -f $LOGSDIR/*

#setup defaults
DURATION=310
OWD_SND=100
OWD_RCV=100



while [[ $# -gt 1 ]]
do
key="$1"
case $key in
    -s|--owdsnd)
    OWD_SND="$2"
    shift # past argument
    ;;
    -r|--owdrcv)
    OWD_RCV="$2"
    shift # past argument
    ;;
    *)
            # unknown option
    ;;
esac
shift # past argument or value
done



  sudo ip netns exec ns_mid tc qdisc change dev veth2 root handle 1: netem delay "$OWD_SND"ms
  sudo ip netns exec ns_mid tc qdisc change dev veth1 root handle 1: netem delay "$OWD_RCV"ms
  
  PEER1_SND="$TEMPDIR/sender_1.sh"
  echo "ntrt -c$CONFDIR/ntrt_snd_meas.ini -m$CONFDIR/ntrt_rmcat7.cmds -t$DURATION &" > $PEER1_SND
  echo "python3 $RUNDIR/stcp.py $TEMPDIR/stcp_1.dat &" >> $PEER1_SND 
  echo "python3 $RUNDIR/stcp.py $TEMPDIR/stcp_2.dat &" >> $PEER1_SND 
  echo "python3 $RUNDIR/stcp.py $TEMPDIR/stcp_3.dat &" >> $PEER1_SND 
  echo "python3 $RUNDIR/stcp.py $TEMPDIR/stcp_4.dat &" >> $PEER1_SND 
  echo "python3 $RUNDIR/stcp.py $TEMPDIR/stcp_5.dat &" >> $PEER1_SND 
  echo "python3 $RUNDIR/stcp.py $TEMPDIR/stcp_6.dat &" >> $PEER1_SND 
  echo "python3 $RUNDIR/stcp.py $TEMPDIR/stcp_7.dat &" >> $PEER1_SND 
  echo "python3 $RUNDIR/stcp.py $TEMPDIR/stcp_8.dat &" >> $PEER1_SND 
  echo "python3 $RUNDIR/stcp.py $TEMPDIR/stcp_9.dat &" >> $PEER1_SND 
  echo "python3 $RUNDIR/stcp.py $TEMPDIR/stcp_10.dat &" >> $PEER1_SND 
  echo -n "./$SENDER" >> $PEER1_SND
  ./$CONFDIR/peer1params.sh >> $PEER1_SND
  chmod 777 $PEER1_SND

  PEER1_RCV="$TEMPDIR/receiver_1.sh"
  echo "ntrt -c$CONFDIR/ntrt_rcv_meas.ini -t$DURATION &" > $PEER1_RCV
  echo "iperf -s -p 1234 &" >> $PEER1_RCV
  echo -n "./$RECEIVER" >> $PEER1_RCV
  ./$CONFDIR/peer1params.sh >> $PEER1_RCV
  echo -n "--save_received_yuvfile=1 " >> $PEER1_RCV 
  chmod 777 $PEER1_RCV


  #start receiver and sender
  sudo ip netns exec $NSRCV ./$PEER1_RCV 2> $LOGSDIR"/"receiver.log &
  sleep 2
  sudo ip netns exec $NSSND ./$PEER1_SND 2> $LOGSDIR"/"sender.log &

cleanup()
# example cleanup function
{
  pkill receiver
  pkill sender
  pkill ntrt
  pkill pyhon3
  ps -ef | grep 'main.sh' | grep -v grep | awk '{print $2}' | xargs kill
}
 
control_c()
# run if user hits control-c
{
  echo -en "\n*** Program is terminated ***\n"
  cleanup
  exit $?
}

trap control_c SIGINT

sleep $DURATION

cleanup


