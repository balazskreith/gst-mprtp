#!/bin/sh
set -x
CMDIN="/tmp/statsrelayer.cmd.in"
FROMDIR="/tmp"
TODIR="temp/.."
MAPPER="packets2csv"
SRCTYPE="mkfifo"
SINKTYPE="file"

sudo pkill statsrelayer
sudo unlink $CMDIN
mkfifo $CMDIN
sleep 1
sudo ./statsrelayer/statsrelayer mkfifo:$CMDIN &
sleep 1

for FLOWNUM in 1 2 3
do
  for ACTOR in "snd_packets" "rcv_packets" "ply_packets"
  do 
    TARGET=$ACTOR"_"$FLOWNUM".csv"
    SRC="$FROMDIR/$TARGET"
    SNK="$TODIR/$TARGET"
    unlink $SRC
    unlink $SNK
    echo "add $TARGET source:mkfifo:$SRC!buffer!mapper:packet2csv!sink:file:$SNK" >> $CMDIN
    sleep 2
  done  
done

echo "lst *;" >> $CMDIN
echo "Commands are done"
