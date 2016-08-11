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


#Create veth pairs
sudo ip link add $S1 type veth peer name $S2M1
sudo ip link add $M2R1 type veth peer name $R1

sudo ip link add $S2 type veth peer name $S2M2
sudo ip link add $M2R2 type veth peer name $R2

sudo ip link add $S3 type veth peer name $S2M3
sudo ip link add $M2R3 type veth peer name $R3


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


#Configure the loopback interface in namespace
sudo ip netns exec $NS_SND ip address add 127.0.0.1/8 dev lo
sudo ip netns exec $NS_SND ip link set dev lo up
sudo ip netns exec $NS_RCV ip address add 127.0.0.1/8 dev lo
sudo ip netns exec $NS_RCV ip link set dev lo up
sudo ip netns exec $NS_MID ip address add 127.0.0.1/8 dev lo
sudo ip netns exec $NS_MID ip link set dev lo up


#Bring up interface in namespace
sudo ip netns exec $NS_SND ip link set dev $S1 up
sudo ip netns exec $NS_SND ip address add 10.0.0.1/30 dev $S1
sudo ip netns exec $NS_MID ip link set dev $S2M1 up
sudo ip netns exec $NS_MID ip address add 10.0.0.2/30 dev $S2M1
sudo ip netns exec $NS_MID ip link set dev $M2R1 up
sudo ip netns exec $NS_MID ip address add 10.0.0.5/30 dev $M2R1
sudo ip netns exec $NS_RCV ip link set dev $R1 up
sudo ip netns exec $NS_RCV ip address add 10.0.0.6/30 dev $R1

sudo ip netns exec $NS_SND ip link set dev $S2 up
sudo ip netns exec $NS_SND ip address add 10.0.1.1/30 dev $S2
sudo ip netns exec $NS_MID ip link set dev $S2M2 up
sudo ip netns exec $NS_MID ip address add 10.0.1.2/30 dev $S2M2
sudo ip netns exec $NS_MID ip link set dev $M2R2 up
sudo ip netns exec $NS_MID ip address add 10.0.1.5/30 dev $M2R2
sudo ip netns exec $NS_RCV ip link set dev $R2 up
sudo ip netns exec $NS_RCV ip address add 10.0.1.6/30 dev $R2

sudo ip netns exec $NS_SND ip link set dev $S3 up
sudo ip netns exec $NS_SND ip address add 10.0.2.1/30 dev $S3
sudo ip netns exec $NS_MID ip link set dev $S2M3 up
sudo ip netns exec $NS_MID ip address add 10.0.2.2/30 dev $S2M3
sudo ip netns exec $NS_MID ip link set dev $M2R3 up
sudo ip netns exec $NS_MID ip address add 10.0.2.5/30 dev $M2R3
sudo ip netns exec $NS_RCV ip link set dev $R3 up
sudo ip netns exec $NS_RCV ip address add 10.0.2.6/30 dev $R3


#Add ip routes
sudo ip netns exec $NS_SND ip route add 10.0.0.4/30 dev $S1 via 10.0.0.2
sudo ip netns exec $NS_RCV ip route add 10.0.0.0/30 dev $R1 via 10.0.0.5

sudo ip netns exec $NS_SND ip route add 10.0.1.4/30 dev $S2 via 10.0.1.2
sudo ip netns exec $NS_RCV ip route add 10.0.1.0/30 dev $R2 via 10.0.1.5

sudo ip netns exec $NS_SND ip route add 10.0.2.4/30 dev $S3 via 10.0.2.2
sudo ip netns exec $NS_RCV ip route add 10.0.2.0/30 dev $R3 via 10.0.2.5

#Add Ip forwarding rule
sudo ip netns exec $NS_MID sysctl -w net.ipv4.ip_forward=1
#dd of=/proc/sys/net/ipv4/ip_forward <<<1

sudo ip netns exec $NS_MID "./scripts/setup_ns_mid.sh"


