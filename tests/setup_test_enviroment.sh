#!/bin/sh

#MpRTP networking, thanks to Jesús Llorente Santos for the help
VETH_CORE0="veth0"
VETH_CORE1="veth1"
VETH_CORE2="veth2"
VETH_CORE3="veth3"
VETH_CORE4="veth4"
VETH_CORE5="veth5"
NS_PDN0="ns0"
NS_PDN1="ns1"


echo "Configure network subflows"
#Remove existing veth pairs
sudo ip link del $VETH_CORE0
sudo ip link del $VETH_CORE2
sudo ip link del $VETH_CORE4
#Create veth pairs
sudo ip link add $VETH_CORE0 type veth peer name $VETH_CORE1
sudo ip link add $VETH_CORE2 type veth peer name $VETH_CORE3
sudo ip link add $VETH_CORE4 type veth peer name $VETH_CORE5
#Bring up
sudo ip link set dev $VETH_CORE0 up
sudo ip link set dev $VETH_CORE1 up
sudo ip link set dev $VETH_CORE2 up
sudo ip link set dev $VETH_CORE3 up
sudo ip link set dev $VETH_CORE4 up
sudo ip link set dev $VETH_CORE5 up


#Remove existing namespace
sudo ip netns del $NS_PDN0
sudo ip netns del $NS_PDN1

#Create the specific namespaces
sudo ip netns add $NS_PDN0
sudo ip netns add $NS_PDN1

#Move the interfaces to the namespace
sudo ip link set $VETH_CORE0 netns $NS_PDN0
sudo ip link set $VETH_CORE1 netns $NS_PDN1
sudo ip link set $VETH_CORE2 netns $NS_PDN0
sudo ip link set $VETH_CORE3 netns $NS_PDN1
sudo ip link set $VETH_CORE4 netns $NS_PDN0
sudo ip link set $VETH_CORE5 netns $NS_PDN1

 

#Configure the loopback interface in namespace
sudo ip netns exec $NS_PDN0 ip address add 127.0.0.1/8 dev lo
sudo ip netns exec $NS_PDN0 ip link set dev lo up
sudo ip netns exec $NS_PDN1 ip address add 127.0.0.1/8 dev lo
sudo ip netns exec $NS_PDN1 ip link set dev lo up

#Bring up interface in namespace
sudo ip netns exec $NS_PDN0 ip link set dev $VETH_CORE0 up
sudo ip netns exec $NS_PDN0 ip address add 10.0.0.1/24 dev $VETH_CORE0
sudo ip netns exec $NS_PDN0 ip link set dev $VETH_CORE2 up
sudo ip netns exec $NS_PDN0 ip address add 10.0.1.1/24 dev $VETH_CORE2
sudo ip netns exec $NS_PDN0 ip link set dev $VETH_CORE4 up
sudo ip netns exec $NS_PDN0 ip address add 10.0.2.1/24 dev $VETH_CORE4

sudo ip netns exec $NS_PDN1 ip link set dev $VETH_CORE1 up
sudo ip netns exec $NS_PDN1 ip address add 10.0.0.2/24 dev $VETH_CORE1
sudo ip netns exec $NS_PDN1 ip link set dev $VETH_CORE3 up
sudo ip netns exec $NS_PDN1 ip address add 10.0.1.2/24 dev $VETH_CORE3
sudo ip netns exec $NS_PDN1 ip link set dev $VETH_CORE5 up
sudo ip netns exec $NS_PDN1 ip address add 10.0.2.2/24 dev $VETH_CORE5

#sudo ip netns exec ns0 tc qdisc add dev veth0 root handle 1: htb default 12
#sudo ip netns exec ns0 tc class add dev veth0 parent 1:1 classid 1:12 htb rate 2048Kbit ceil 2048Kbit
#sudo ip netns exec ns0 tc qdisc add dev veth0 parent 1:12 netem delay 100ms 1ms

#sudo ip netns exec ns0 tc qdisc add dev veth2 root handle 2: htb default 22
#sudo ip netns exec ns0 tc class add dev veth2 parent 2:2 classid 2:22 htb rate 2048Kbit ceil 2048Kbit
#sudo ip netns exec ns0 tc qdisc add dev veth2 parent 2:22 netem delay 100ms 1ms

#sudo ip netns exec ns0 tc qdisc add dev veth4 root handle 4: htb default 44
#sudo ip netns exec ns0 tc class add dev veth4 parent 4:4 classid 4:44 htb rate 2048Kbit ceil 2048Kbit
#sudo ip netns exec ns0 tc qdisc add dev veth4 parent 4:44 netem delay 100ms 1ms




