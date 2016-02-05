#!/bin/bash
VETH0_CMDS="./run_veth0_cc.sh"
#VETH0_CMDS="./run_veth0_const.sh"
VETH2_CMDS="./run_veth2_const.sh"
VETH4_CMDS="./run_veth4_const.sh"
SERVER="./server"
CLIENT="./client"
TARGET_DIR="logs"
BW_SUM_LOG='bw_sum.csv'
PROFILE=$1

let "ACTIVE_SUB1=$PROFILE&1"
let "ACTIVE_SUB2=$PROFILE&2"
let "ACTIVE_SUB3=$PROFILE&4"

eval rm $TARGET_DIR"/*"

if [ "$ACTIVE_SUB1" -eq 1 ]
then
  sudo ip netns exec ns0 $VETH0_CMDS > $TARGET_DIR"/"veth0_bw.txt &
fi

if [ "$ACTIVE_SUB2" -eq 2 ]
then
  sudo ip netns exec ns0 $VETH2_CMDS > $TARGET_DIR"/"veth2_bw.txt &
fi

if [ "$ACTIVE_SUB3" -eq 4 ]
then
  sudo ip netns exec ns0 $VETH4_CMDS > $TARGET_DIR"/"veth4_bw.txt &
fi

sudo ip netns exec ns0 $SERVER "--profile="$PROFILE 2> $TARGET_DIR"/"server.log &
sudo ip netns exec ns1 $CLIENT "--profile="$PROFILE 2> $TARGET_DIR"/"client.log &

cleanup()
# example cleanup function
{
  pkill client
  pkill server
  ps -ef | grep 'run_veth0_const.sh' | grep -v grep | awk '{print $2}' | xargs kill
  ps -ef | grep 'run_veth0_cc.sh' | grep -v grep | awk '{print $2}' | xargs kill
  ps -ef | grep 'run_veth2_const.sh' | grep -v grep | awk '{print $2}' | xargs kill
  ps -ef | grep 'run_veth4_const.sh' | grep -v grep | awk '{print $2}' | xargs kill
}
 
control_c()
# run if user hits control-c
{
  echo -en "\n*** Ouch! Exiting ***\n"
  cleanup
  exit $?
}

trap control_c SIGINT

#sudo ip netns exec ns0 ./run_veth0_const.sh > veth0_bw.txt &
sleep 1
for j in `seq 1 120`;
do
  for i in `seq 1 5`;
  do
    echo "0," >> $TARGET_DIR"/"$BW_SUM_LOG
    sleep 1
  done
  echo $j"*5 seconds"
  ./run_test_evaluator.sh $PROFILE
done
sleep 1

cleanup


