#!/bin/bash
set -x
#BW/8*1000 = RATE_IN_BYTES-> RATE/10 * 3 -> 30% of the bytes. => 1000/8 * 3/10 = 125 * 3 / 10 = 37,5
VETH0="veth0"
BW=1000
LIMIT=0
let "LIMIT=$BW*37,5"
LATENCY=100
BURST=15400

tc qdisc del dev "$VETH0" root
tc qdisc add dev "$VETH0" root handle 1: netem delay "$LATENCY"ms
#tc qdisc add dev "$VETH0" parent 1: handle 2: tbf rate "$BW"kbit burst "$BURST" latency 300ms minburst 1540

#ipfw pipe flush
#ipfw add 100 pipe 1 ip from 10.0.0.1 to 10.0.0.2
#ipfw pipe 1 config bw 1000Kbit/s queue 300Kbit/s

VETH2="veth2"
BW=5000
let "LIMIT=$BW*37,5"
LATENCY=100
BURST=15400

tc qdisc del dev "$VETH2" root
tc qdisc add dev "$VETH2" root handle 1: netem delay "$LATENCY"ms 
tc qdisc add dev "$VETH2" parent 1: handle 2: tbf rate "$BW"kbit burst "$BURST" latency 300ms minburst 1540

VETH4="veth4"
BW=5000
let "LIMIT=$BW*37,5"
LATENCY=100
BURST=100

tc qdisc del dev "$VETH4" root
tc qdisc add dev "$VETH4" root handle 1: netem delay "$LATENCY"ms 
tc qdisc add dev "$VETH4" parent 1: handle 2: tbf rate "$BW"kbit burst "$BURST" latency 300ms minburst 1540

VETH6="veth6"
BW=5000
let "LIMIT=$BW*37,5"
LATENCY=100
BURST=15400

tc qdisc del dev "$VETH6" root
tc qdisc add dev "$VETH6" root handle 1: netem delay "$LATENCY"ms 
tc qdisc add dev "$VETH6" parent 1: handle 2: tbf rate "$BW"kbit burst "$BURST" latency 300ms minburst 1540

VETH8="veth8"
BW=5000
let "LIMIT=$BW*37,5"
LATENCY=100
BURST=15400

tc qdisc del dev "$VETH8" root
tc qdisc add dev "$VETH8" root handle 1: netem delay "$LATENCY"ms 
tc qdisc add dev "$VETH8" parent 1: handle 2: tbf rate "$BW"kbit burst "$BURST" latency 300ms minburst 1540


