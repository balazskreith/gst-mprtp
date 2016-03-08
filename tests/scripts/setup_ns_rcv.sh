#!/bin/bash
VETH1="veth1"
BW=1000
LIMIT=0
let "LIMIT=$BW*37,5"
LATENCY=50
echo "Setup $VETH0 for "$LATENCY"ms and "$BW"Kbit rate with "$LIMIT" queue limit"

tc qdisc del dev "$VETH1" root
tc qdisc add dev "$VETH1" root handle 1: netem delay "$LATENCY"ms 
tc qdisc add dev "$VETH1" parent 1: handle 2: tbf rate 1mbit burst "$BW"kbit limit $LIMIT
