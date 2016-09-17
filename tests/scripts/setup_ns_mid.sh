#!/bin/bash
set -x
VETH1="veth1"
BW=1000
LATENCY=100
BURST=15400

tc qdisc del dev "$VETH1" root
tc qdisc add dev "$VETH1" root handle 1: netem delay "$LATENCY"ms
tc qdisc add dev "$VETH1" parent 1: handle 2: tbf rate "$BW"kbit burst "$BURST" latency 300ms minburst 1540

#tc qdisc add dev "$VETH1" root handle 1: tbf rate "$BW"kbit burst "$BURST" latency 300ms minburst 1540
#tc qdisc add dev "$VETH1" parent 1: handle 2: netem delay "$LATENCY"ms

#dummynet version
#ipfw pipe flush
#ipfw add 100 pipe 1 ip from 10.0.0.1 to 10.0.0.2
#ipfw pipe 1 config bw 3500Kbit/s queue 1050Kbit/s

VETH2="veth2"
BW=1000
LATENCY=100
BURST=15400

tc qdisc del dev "$VETH2" root
tc qdisc add dev "$VETH2" root handle 1: netem delay "$LATENCY"ms 
tc qdisc add dev "$VETH2" parent 1: handle 2: tbf rate "$BW"kbit burst "$BURST" latency 300ms minburst 1540

#tc qdisc add dev "$VETH2" root handle 1: tbf rate "$BW"kbit burst "$BURST" latency 300ms minburst 1540
#tc qdisc add dev "$VETH2" parent 1: handle 2: netem delay "$LATENCY"ms


