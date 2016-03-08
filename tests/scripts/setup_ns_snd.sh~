#!/bin/bash
#BW/8*1000 = RATE_IN_BYTES-> RATE/10 * 3 -> 30% of the bytes. => 1000/8 * 3/10 = 125 * 3 / 10 = 37,5
VETH0="veth0"
BW=1000
LIMIT=0
let "LIMIT=$BW*37,5"
LATENCY=50
echo "Setup $VETH0 for "$LATENCY"ms and "$BW"Kbit rate with "$LIMIT" queue limit"

tc qdisc del dev "$VETH0" root
tc qdisc add dev "$VETH0" root handle 1: netem delay "$LATENCY"ms 
tc qdisc add dev "$VETH0" parent 1: handle 2: tbf rate 1mbit burst "$BW"kbit limit $LIMIT
