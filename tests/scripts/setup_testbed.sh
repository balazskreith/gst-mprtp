#!/bin/sh
set -x
#MpRTP networking, thanks to Jesús Llorente Santos for the help

S1="veth0"
S2M1="veth1"
M2R1="veth2"
R1="veth3"

S2="veth4"
S2M2="veth5"
M2R2="veth6"
R2="veth7"

S3="veth8"
S2M3="veth9"
M2R3="veth10"
R3="veth11"

S4="veth12"
S2M4="veth13"
M2R4="veth14"
R4="veth15"

S5="veth16"
S2M5="veth17"
M2R5="veth18"
R5="veth19"

S6="veth20"
S2M6="veth21"
M2R6="veth22"
R6="veth23"

S7="veth24"
S2M7="veth25"
M2R7="veth26"
R7="veth27"

S8="veth28"
S2M8="veth29"
M2R8="veth30"
R8="veth31"

S9="veth32"
S2M9="veth33"
M2R9="veth34"
R9="veth35"

S10="veth36"
S2M10="veth37"
M2R10="veth38"
R10="veth39"


NS_SND="ns_snd"
NS_RCV="ns_rcv"
NS_MID="ns_mid"


#Remove existing namespace
sudo ip netns del $NS_SND
sudo ip netns del $NS_RCV
sudo ip netns del $NS_MID


#Remove existing veth pairs
sudo ip link del $S1
sudo ip link del $R1
sudo ip link del $S2M1
sudo ip link del $M2R1

sudo ip link del $S2
sudo ip link del $R2
sudo ip link del $S2M2
sudo ip link del $M2R2

sudo ip link del $S3
sudo ip link del $R3
sudo ip link del $S2M3
sudo ip link del $M2R3

sudo ip link del $S4
sudo ip link del $R4
sudo ip link del $S2M4
sudo ip link del $M2R4

sudo ip link del $S5
sudo ip link del $R5
sudo ip link del $S2M5
sudo ip link del $M2R5

sudo ip link del $S6
sudo ip link del $R6
sudo ip link del $S2M6
sudo ip link del $M2R6

sudo ip link del $S7
sudo ip link del $R7
sudo ip link del $S2M7
sudo ip link del $M2R7

sudo ip link del $S8
sudo ip link del $R8
sudo ip link del $S2M8
sudo ip link del $M2R8

sudo ip link del $S9
sudo ip link del $R9
sudo ip link del $S2M9
sudo ip link del $M2R9

sudo ip link del $S10
sudo ip link del $R10
sudo ip link del $S2M10
sudo ip link del $M2R10


#Create veth pairs
sudo ip link add $S1 type veth peer name $S2M1
sudo ip link add $M2R1 type veth peer name $R1

sudo ip link add $S2 type veth peer name $S2M2
sudo ip link add $M2R2 type veth peer name $R2

sudo ip link add $S3 type veth peer name $S2M3
sudo ip link add $M2R3 type veth peer name $R3

sudo ip link add $S4 type veth peer name $S2M4
sudo ip link add $M2R4 type veth peer name $R4

sudo ip link add $S5 type veth peer name $S2M5
sudo ip link add $M2R5 type veth peer name $R5

sudo ip link add $S6 type veth peer name $S2M6
sudo ip link add $M2R6 type veth peer name $R6

sudo ip link add $S7 type veth peer name $S2M7
sudo ip link add $M2R7 type veth peer name $R7

sudo ip link add $S8 type veth peer name $S2M8
sudo ip link add $M2R8 type veth peer name $R8

sudo ip link add $S9 type veth peer name $S2M9
sudo ip link add $M2R9 type veth peer name $R9

sudo ip link add $S10 type veth peer name $S2M10
sudo ip link add $M2R10 type veth peer name $R10

#Bring up
sudo ip link set dev $S1 up
sudo ip link set dev $S2M1 up
sudo ip link set dev $M2R1 up
sudo ip link set dev $R1 up

sudo ip link set dev $S2 up
sudo ip link set dev $S2M2 up
sudo ip link set dev $M2R2 up
sudo ip link set dev $R2 up

sudo ip link set dev $S3 up
sudo ip link set dev $S2M3 up
sudo ip link set dev $M2R3 up
sudo ip link set dev $R3 up

sudo ip link set dev $S4 up
sudo ip link set dev $S2M4 up
sudo ip link set dev $M2R4 up
sudo ip link set dev $R4 up

sudo ip link set dev $S5 up
sudo ip link set dev $S2M5 up
sudo ip link set dev $M2R5 up
sudo ip link set dev $R5 up

sudo ip link set dev $S6 up
sudo ip link set dev $S2M6 up
sudo ip link set dev $M2R6 up
sudo ip link set dev $R6 up

sudo ip link set dev $S7 up
sudo ip link set dev $S2M7 up
sudo ip link set dev $M2R7 up
sudo ip link set dev $R7 up

sudo ip link set dev $S8 up
sudo ip link set dev $S2M8 up
sudo ip link set dev $M2R8 up
sudo ip link set dev $R8 up

sudo ip link set dev $S9 up
sudo ip link set dev $S2M9 up
sudo ip link set dev $M2R9 up
sudo ip link set dev $R9 up

sudo ip link set dev $S10 up
sudo ip link set dev $S2M10 up
sudo ip link set dev $M2R10 up
sudo ip link set dev $R10 up

#Create the specific namespaces
sudo ip netns add $NS_SND
sudo ip netns add $NS_RCV
sudo ip netns add $NS_MID


#Move the interfaces to the namespace
sudo ip link set $S1 netns $NS_SND
sudo ip link set $S2M1 netns $NS_MID
sudo ip link set $M2R1 netns $NS_MID
sudo ip link set $R1 netns $NS_RCV

sudo ip link set $S2 netns $NS_SND
sudo ip link set $S2M2 netns $NS_MID
sudo ip link set $M2R2 netns $NS_MID
sudo ip link set $R2 netns $NS_RCV

sudo ip link set $S3 netns $NS_SND
sudo ip link set $S2M3 netns $NS_MID
sudo ip link set $M2R3 netns $NS_MID
sudo ip link set $R3 netns $NS_RCV

sudo ip link set $S4 netns $NS_SND
sudo ip link set $S2M4 netns $NS_MID
sudo ip link set $M2R4 netns $NS_MID
sudo ip link set $R4 netns $NS_RCV

sudo ip link set $S5 netns $NS_SND
sudo ip link set $S2M5 netns $NS_MID
sudo ip link set $M2R5 netns $NS_MID
sudo ip link set $R5 netns $NS_RCV

sudo ip link set $S6 netns $NS_SND
sudo ip link set $S2M6 netns $NS_MID
sudo ip link set $M2R6 netns $NS_MID
sudo ip link set $R6 netns $NS_RCV

sudo ip link set $S7 netns $NS_SND
sudo ip link set $S2M7 netns $NS_MID
sudo ip link set $M2R7 netns $NS_MID
sudo ip link set $R7 netns $NS_RCV

sudo ip link set $S8 netns $NS_SND
sudo ip link set $S2M8 netns $NS_MID
sudo ip link set $M2R8 netns $NS_MID
sudo ip link set $R8 netns $NS_RCV

sudo ip link set $S9 netns $NS_SND
sudo ip link set $S2M9 netns $NS_MID
sudo ip link set $M2R9 netns $NS_MID
sudo ip link set $R9 netns $NS_RCV

sudo ip link set $S10 netns $NS_SND
sudo ip link set $S2M10 netns $NS_MID
sudo ip link set $M2R10 netns $NS_MID
sudo ip link set $R10 netns $NS_RCV


#Configure the loopback interface in namespace
sudo ip netns exec $NS_SND ip address add 127.0.0.1/8 dev lo
sudo ip netns exec $NS_SND ip link set dev lo up
sudo ip netns exec $NS_RCV ip address add 127.0.0.1/8 dev lo
sudo ip netns exec $NS_RCV ip link set dev lo up
sudo ip netns exec $NS_MID ip address add 127.0.0.1/8 dev lo
sudo ip netns exec $NS_MID ip link set dev lo up


#J> Setup LXBs in NS_MID
sudo ip netns exec $NS_MID ip link add name br0 type bridge
sudo ip netns exec $NS_MID ip link set dev br0 up
sudo ip netns exec $NS_MID ip link add name br1 type bridge
sudo ip netns exec $NS_MID ip link set dev br1 up
sudo ip netns exec $NS_MID ip link add name br2 type bridge
sudo ip netns exec $NS_MID ip link set dev br2 up
sudo ip netns exec $NS_MID ip link add name br3 type bridge
sudo ip netns exec $NS_MID ip link set dev br3 up
sudo ip netns exec $NS_MID ip link add name br4 type bridge
sudo ip netns exec $NS_MID ip link set dev br4 up

sudo ip netns exec $NS_MID ip link add name br5 type bridge
sudo ip netns exec $NS_MID ip link set dev br5 up
sudo ip netns exec $NS_MID ip link add name br6 type bridge
sudo ip netns exec $NS_MID ip link set dev br6 up
sudo ip netns exec $NS_MID ip link add name br7 type bridge
sudo ip netns exec $NS_MID ip link set dev br7 up
sudo ip netns exec $NS_MID ip link add name br8 type bridge
sudo ip netns exec $NS_MID ip link set dev br8 up
sudo ip netns exec $NS_MID ip link add name br9 type bridge
sudo ip netns exec $NS_MID ip link set dev br9 up

## Bring up veths in MID
sudo ip netns exec $NS_MID ip link set dev $S2M1 up
sudo ip netns exec $NS_MID ip link set dev $M2R1 up
sudo ip netns exec $NS_MID ip link set dev $S2M2 up
sudo ip netns exec $NS_MID ip link set dev $M2R2 up
sudo ip netns exec $NS_MID ip link set dev $S2M3 up
sudo ip netns exec $NS_MID ip link set dev $M2R3 up
sudo ip netns exec $NS_MID ip link set dev $S2M4 up
sudo ip netns exec $NS_MID ip link set dev $M2R4 up
sudo ip netns exec $NS_MID ip link set dev $S2M5 up
sudo ip netns exec $NS_MID ip link set dev $M2R5 up
sudo ip netns exec $NS_MID ip link set dev $S2M6 up
sudo ip netns exec $NS_MID ip link set dev $M2R6 up
sudo ip netns exec $NS_MID ip link set dev $S2M7 up
sudo ip netns exec $NS_MID ip link set dev $M2R7 up
sudo ip netns exec $NS_MID ip link set dev $S2M8 up
sudo ip netns exec $NS_MID ip link set dev $M2R8 up
sudo ip netns exec $NS_MID ip link set dev $S2M9 up
sudo ip netns exec $NS_MID ip link set dev $M2R9 up
sudo ip netns exec $NS_MID ip link set dev $S2M10 up
sudo ip netns exec $NS_MID ip link set dev $M2R10 up
## Add veth to LXBs
sudo ip netns exec $NS_MID ip link set $S2M1 master br0
sudo ip netns exec $NS_MID ip link set $M2R1 master br0
sudo ip netns exec $NS_MID ip link set $S2M2 master br1
sudo ip netns exec $NS_MID ip link set $M2R2 master br1
sudo ip netns exec $NS_MID ip link set $S2M3 master br2
sudo ip netns exec $NS_MID ip link set $M2R3 master br2
sudo ip netns exec $NS_MID ip link set $S2M4 master br3
sudo ip netns exec $NS_MID ip link set $M2R4 master br3
sudo ip netns exec $NS_MID ip link set $S2M5 master br4
sudo ip netns exec $NS_MID ip link set $M2R5 master br4

sudo ip netns exec $NS_MID ip link set $S2M6 master br5
sudo ip netns exec $NS_MID ip link set $M2R6 master br5
sudo ip netns exec $NS_MID ip link set $S2M7 master br6
sudo ip netns exec $NS_MID ip link set $M2R7 master br6
sudo ip netns exec $NS_MID ip link set $S2M8 master br7
sudo ip netns exec $NS_MID ip link set $M2R8 master br7
sudo ip netns exec $NS_MID ip link set $S2M9 master br8
sudo ip netns exec $NS_MID ip link set $M2R9 master br8
sudo ip netns exec $NS_MID ip link set $S2M10 master br9
sudo ip netns exec $NS_MID ip link set $M2R10 master br9


#Bring up interface in namespace
sudo ip netns exec $NS_SND ip link set dev $S1 up
sudo ip netns exec $NS_SND ip address add 10.0.0.1/24 dev $S1
sudo ip netns exec $NS_RCV ip link set dev $R1 up
sudo ip netns exec $NS_RCV ip address add 10.0.0.6/24 dev $R1

sudo ip netns exec $NS_SND ip link set dev $S2 up
sudo ip netns exec $NS_SND ip address add 10.0.1.1/24 dev $S2
sudo ip netns exec $NS_RCV ip link set dev $R2 up
sudo ip netns exec $NS_RCV ip address add 10.0.1.6/24 dev $R2

sudo ip netns exec $NS_SND ip link set dev $S3 up
sudo ip netns exec $NS_SND ip address add 10.0.2.1/24 dev $S3
sudo ip netns exec $NS_RCV ip link set dev $R3 up
sudo ip netns exec $NS_RCV ip address add 10.0.2.6/24 dev $R3

sudo ip netns exec $NS_SND ip link set dev $S4 up
sudo ip netns exec $NS_SND ip address add 10.0.3.1/24 dev $S4
sudo ip netns exec $NS_RCV ip link set dev $R4 up
sudo ip netns exec $NS_RCV ip address add 10.0.3.6/24 dev $R4

sudo ip netns exec $NS_SND ip link set dev $S5 up
sudo ip netns exec $NS_SND ip address add 10.0.4.1/24 dev $S5
sudo ip netns exec $NS_RCV ip link set dev $R5 up
sudo ip netns exec $NS_RCV ip address add 10.0.4.6/24 dev $R5

sudo ip netns exec $NS_SND ip link set dev $S6 up
sudo ip netns exec $NS_SND ip address add 10.0.5.1/24 dev $S6
sudo ip netns exec $NS_RCV ip link set dev $R6 up
sudo ip netns exec $NS_RCV ip address add 10.0.5.6/24 dev $R6

sudo ip netns exec $NS_SND ip link set dev $S7 up
sudo ip netns exec $NS_SND ip address add 10.0.6.1/24 dev $S7
sudo ip netns exec $NS_RCV ip link set dev $R7 up
sudo ip netns exec $NS_RCV ip address add 10.0.6.6/24 dev $R7

sudo ip netns exec $NS_SND ip link set dev $S8 up
sudo ip netns exec $NS_SND ip address add 10.0.7.1/24 dev $S8
sudo ip netns exec $NS_RCV ip link set dev $R8 up
sudo ip netns exec $NS_RCV ip address add 10.0.7.6/24 dev $R8

sudo ip netns exec $NS_SND ip link set dev $S9 up
sudo ip netns exec $NS_SND ip address add 10.0.8.1/24 dev $S9
sudo ip netns exec $NS_RCV ip link set dev $R9 up
sudo ip netns exec $NS_RCV ip address add 10.0.8.6/24 dev $R9

sudo ip netns exec $NS_SND ip link set dev $S10 up
sudo ip netns exec $NS_SND ip address add 10.0.9.1/24 dev $S10
sudo ip netns exec $NS_RCV ip link set dev $R10 up
sudo ip netns exec $NS_RCV ip address add 10.0.9.6/24 dev $R10


#Add IP forwarding rule
sudo ip netns exec $NS_MID sysctl -w net.ipv4.ip_forward=1
#dd of=/proc/sys/net/ipv4/ip_forward <<<1

sudo ip netns exec $NS_MID "./scripts/setup_ns_mid.sh"
scripts/setup_statsrelayer.sh
