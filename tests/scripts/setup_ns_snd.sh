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


VETH10="veth10"
BW=5000
let "LIMIT=$BW*37,5"
LATENCY=100
BURST=15400

tc qdisc del dev "$VETH10" root
tc qdisc add dev "$VETH10" root handle 1: netem delay "$LATENCY"ms
tc qdisc add dev "$VETH10" parent 1: handle 2: tbf rate "$BW"kbit burst "$BURST" latency 300ms minburst 1540

VETH12="veth12"
BW=5000
let "LIMIT=$BW*37,5"
LATENCY=100
BURST=15400

tc qdisc del dev "$VETH12" root
tc qdisc add dev "$VETH12" root handle 1: netem delay "$LATENCY"ms
tc qdisc add dev "$VETH12" parent 1: handle 2: tbf rate "$BW"kbit burst "$BURST" latency 300ms minburst 1540

VETH14="veth14"
BW=5000
let "LIMIT=$BW*37,5"
LATENCY=100
BURST=15400

tc qdisc del dev "$VETH14" root
tc qdisc add dev "$VETH14" root handle 1: netem delay "$LATENCY"ms
tc qdisc add dev "$VETH14" parent 1: handle 2: tbf rate "$BW"kbit burst "$BURST" latency 300ms minburst 1540

VETH16="veth16"
BW=5000
let "LIMIT=$BW*37,5"
LATENCY=100
BURST=15400

tc qdisc del dev "$VETH16" root
tc qdisc add dev "$VETH16" root handle 1: netem delay "$LATENCY"ms
tc qdisc add dev "$VETH16" parent 1: handle 2: tbf rate "$BW"kbit burst "$BURST" latency 300ms minburst 1540




VETH18="veth18"
BW=1000
LIMIT=0
let "LIMIT=$BW*37,5"
LATENCY=100
BURST=15400

tc qdisc del dev "$VETH18" root
tc qdisc add dev "$VETH18" root handle 1: netem delay "$LATENCY"ms
tc qdisc add dev "$VETH18" parent 1: handle 2: tbf rate "$BW"kbit burst "$BURST" latency 300ms minburst 1540


VETH20="veth20"
BW=5000
let "LIMIT=$BW*37,5"
LATENCY=100
BURST=15400

tc qdisc del dev "$VETH20" root
tc qdisc add dev "$VETH20" root handle 1: netem delay "$LATENCY"ms
tc qdisc add dev "$VETH20" parent 1: handle 2: tbf rate "$BW"kbit burst "$BURST" latency 300ms minburst 1540

VETH22="veth22"
BW=5000
let "LIMIT=$BW*37,5"
LATENCY=100
BURST=100

tc qdisc del dev "$VETH22" root
tc qdisc add dev "$VETH22" root handle 1: netem delay "$LATENCY"ms
tc qdisc add dev "$VETH22" parent 1: handle 2: tbf rate "$BW"kbit burst "$BURST" latency 300ms minburst 1540

VETH24="veth24"
BW=5000
let "LIMIT=$BW*37,5"
LATENCY=100
BURST=15400

tc qdisc del dev "$VETH24" root
tc qdisc add dev "$VETH24" root handle 1: netem delay "$LATENCY"ms
tc qdisc add dev "$VETH24" parent 1: handle 2: tbf rate "$BW"kbit burst "$BURST" latency 300ms minburst 1540

VETH26="veth26"
BW=5000
let "LIMIT=$BW*37,5"
LATENCY=100
BURST=15400

tc qdisc del dev "$VETH26" root
tc qdisc add dev "$VETH26" root handle 1: netem delay "$LATENCY"ms
tc qdisc add dev "$VETH26" parent 1: handle 2: tbf rate "$BW"kbit burst "$BURST" latency 300ms minburst 1540


VETH28="veth28"
BW=5000
let "LIMIT=$BW*37,5"
LATENCY=100
BURST=15400

tc qdisc del dev "$VETH28" root
tc qdisc add dev "$VETH28" root handle 1: netem delay "$LATENCY"ms
tc qdisc add dev "$VETH28" parent 1: handle 2: tbf rate "$BW"kbit burst "$BURST" latency 300ms minburst 1540

VETH30="veth30"
BW=5000
let "LIMIT=$BW*37,5"
LATENCY=100
BURST=15400

tc qdisc del dev "$VETH30" root
tc qdisc add dev "$VETH30" root handle 1: netem delay "$LATENCY"ms
tc qdisc add dev "$VETH30" parent 1: handle 2: tbf rate "$BW"kbit burst "$BURST" latency 300ms minburst 1540

VETH32="veth32"
BW=5000
let "LIMIT=$BW*37,5"
LATENCY=100
BURST=15400

tc qdisc del dev "$VETH32" root
tc qdisc add dev "$VETH32" root handle 1: netem delay "$LATENCY"ms
tc qdisc add dev "$VETH32" parent 1: handle 2: tbf rate "$BW"kbit burst "$BURST" latency 300ms minburst 1540

VETH34="veth34"
BW=5000
let "LIMIT=$BW*37,5"
LATENCY=100
BURST=15400

tc qdisc del dev "$VETH34" root
tc qdisc add dev "$VETH34" root handle 1: netem delay "$LATENCY"ms
tc qdisc add dev "$VETH34" parent 1: handle 2: tbf rate "$BW"kbit burst "$BURST" latency 300ms minburst 1540
