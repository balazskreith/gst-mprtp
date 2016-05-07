#!/bin/bash
set -x

VETH1="veth1"
BW=1000
LIMIT=0
let "LIMIT=$BW*37,5"
LATENCY=100
BURST=15400

tc qdisc del dev "$VETH1" root
tc qdisc add dev "$VETH1" root handle 1: netem delay "$LATENCY"ms
tc qdisc add dev "$VETH1" parent 1: handle 2: tbf rate "$BW"kbit burst "$BURST" latency 300ms minburst 1540


VETH3="veth3"
BW=1000
LIMIT=0
let "LIMIT=$BW*37,5"
LATENCY=100
BURST=15400

tc qdisc del dev "$VETH3" root
tc qdisc add dev "$VETH3" root handle 2: netem delay "$LATENCY"ms 
tc qdisc add dev "$VETH3" parent 2: handle 2: tbf rate "$BW"kbit burst "$BURST" latency 300ms minburst 1540

VETH5="veth5"
BW=1000
BURST=15400
LIMIT=0
let "LIMIT=$BW*37,5"
LATENCY=100

tc qdisc del dev "$VETH5" root
tc qdisc add dev "$VETH5" root handle 3: netem delay "$LATENCY"ms 
tc qdisc add dev "$VETH5" parent 3: handle 2: tbf rate "$BW"kbit burst "$BURST" latency 300ms minburst 1540


VETH7="veth7"
BW=1000
BURST=15400
LIMIT=0
let "LIMIT=$BW*37,5"
LATENCY=100

tc qdisc del dev "$VETH7" root
tc qdisc add dev "$VETH7" root handle 4: netem delay "$LATENCY"ms 
tc qdisc add dev "$VETH7" parent 4: handle 2: tbf rate "$BW"kbit burst "$BURST" latency 300ms minburst 1540


VETH9="veth9"
BW=1000
BURST=15400
LIMIT=0
let "LIMIT=$BW*37,5"
LATENCY=100

tc qdisc del dev "$VETH9" root
tc qdisc add dev "$VETH9" root handle 5: netem delay "$LATENCY"ms 
tc qdisc add dev "$VETH9" parent 5: handle 2: tbf rate "$BW"kbit burst "$BURST" latency 300ms minburst 1540


