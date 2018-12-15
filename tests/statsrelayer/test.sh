#!/bin/bash
CMDIN="/tmp/statsrelayer.cmds.in"
MKFIFOIN="/tmp/statsrelayer.test.mkfifo.in"
MKFIFOOUT="/tmp/statsrelayer.test.mkfifo.out"
UDSOCKET="/tmp/statsrelayer.test.udsocket"
FILE="statsrelayer.test.file"

G1="testgroup1"
G2="testgroup2"
G3="testgroup3"
unlink $CMDIN
unlink $MKFIFOIN
unlink $MKFIFOOUT
unlink $UDSOCKET
unlink $FILE

#set -x
./statsrelayer mkfifo:$CMDIN &
# Add groups to read and collect
sleep 1
echo "add $G1 mkfifo:$MKFIFOIN|unix_dgram_socket:$UDSOCKET;" >> $CMDIN
echo "add $G2 unix_dgram_socket:$UDSOCKET|file:$FILE:ab;" >> $CMDIN
sleep 1
echo "lst *;" >> $CMDIN
echo "TESTSTRING" >> $MKFIFOIN
sleep 1
echo "fls $G1" >> $CMDIN
sleep 2
echo "fls $G2" >> $CMDIN
sleep 2
echo "rem $G1;rem $G2" >> $CMDIN
sleep 1
echo "add $G3 file:$FILE|file:$FILE:ab 32;" >> $CMDIN
echo "lst *;" >> $CMDIN
sleep 1
echo "fls $G3" >> $CMDIN
sleep 2
echo "ext" >> $CMDIN
sleep 5
sudo pkill statsrelayer
#echo "TESTSTRING" >> $MKFIFOIN
#cat $MKFIFOOUT 

