#!/bin/bash
set -x

VETH1="veth1"
BW=1000
LIMIT=0
let "LIMIT=$BW*37,5"
LATENCY=100
BURST=15400

#tc qdisc del dev "$VETH1" root
#tc qdisc add dev "$VETH1" root handle 1: netem delay "$LATENCY"ms
#tc qdisc add dev "$VETH1" parent 1: handle 2: tbf rate "$BW"kbit burst "$BURST" latency 300ms minburst 1540

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


VETH11="veth11"
BW=1000
BURST=15400
LIMIT=0
let "LIMIT=$BW*37,5"
LATENCY=100

tc qdisc del dev "$VETH11" root
tc qdisc add dev "$VETH11" root handle 6: netem delay "$LATENCY"ms
tc qdisc add dev "$VETH11" parent 6: handle 2: tbf rate "$BW"kbit burst "$BURST" latency 300ms minburst 1540


VETH13="veth13"
BW=1000
BURST=15400
LIMIT=0
let "LIMIT=$BW*37,5"
LATENCY=100

tc qdisc del dev "$VETH13" root
tc qdisc add dev "$VETH13" root handle 7: netem delay "$LATENCY"ms
tc qdisc add dev "$VETH13" parent 7: handle 2: tbf rate "$BW"kbit burst "$BURST" latency 300ms minburst 1540



VETH15="veth15"
BW=1000
LIMIT=0
let "LIMIT=$BW*37,5"
LATENCY=100
BURST=15400

tc qdisc del dev "$VETH15" root
tc qdisc add dev "$VETH15" root handle 1: netem delay "$LATENCY"ms
tc qdisc add dev "$VETH15" parent 1: handle 2: tbf rate "$BW"kbit burst "$BURST" latency 300ms minburst 1540

VETH17="veth17"
BW=1000
LIMIT=0
let "LIMIT=$BW*37,5"
LATENCY=100
BURST=15400

tc qdisc del dev "$VETH17" root
tc qdisc add dev "$VETH17" root handle 2: netem delay "$LATENCY"ms
tc qdisc add dev "$VETH17" parent 2: handle 2: tbf rate "$BW"kbit burst "$BURST" latency 300ms minburst 1540

VETH19="veth19"
BW=1000
BURST=15400
LIMIT=0
let "LIMIT=$BW*37,5"
LATENCY=100

tc qdisc del dev "$VETH19" root
tc qdisc add dev "$VETH19" root handle 3: netem delay "$LATENCY"ms
tc qdisc add dev "$VETH19" parent 3: handle 2: tbf rate "$BW"kbit burst "$BURST" latency 300ms minburst 1540


VETH21="veth21"
BW=1000
BURST=15400
LIMIT=0
let "LIMIT=$BW*37,5"
LATENCY=100

tc qdisc del dev "$VETH21" root
tc qdisc add dev "$VETH21" root handle 4: netem delay "$LATENCY"ms
tc qdisc add dev "$VETH21" parent 4: handle 2: tbf rate "$BW"kbit burst "$BURST" latency 300ms minburst 1540


VETH23="veth23"
BW=1000
BURST=15400
LIMIT=0
let "LIMIT=$BW*37,5"
LATENCY=100

tc qdisc del dev "$VETH23" root
tc qdisc add dev "$VETH23" root handle 5: netem delay "$LATENCY"ms
tc qdisc add dev "$VETH23" parent 5: handle 2: tbf rate "$BW"kbit burst "$BURST" latency 300ms minburst 1540


VETH25="veth25"
BW=1000
BURST=15400
LIMIT=0
let "LIMIT=$BW*37,5"
LATENCY=100

tc qdisc del dev "$VETH25" root
tc qdisc add dev "$VETH25" root handle 6: netem delay "$LATENCY"ms
tc qdisc add dev "$VETH25" parent 6: handle 2: tbf rate "$BW"kbit burst "$BURST" latency 300ms minburst 1540


VETH27="veth27"
BW=1000
BURST=15400
LIMIT=0
let "LIMIT=$BW*37,5"
LATENCY=100

tc qdisc del dev "$VETH27" root
tc qdisc add dev "$VETH27" root handle 7: netem delay "$LATENCY"ms
tc qdisc add dev "$VETH27" parent 7: handle 2: tbf rate "$BW"kbit burst "$BURST" latency 300ms minburst 1540
