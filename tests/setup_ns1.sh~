tc qdisc add dev veth1 root netem delay 100ms
tc qdisc add dev veth3 root netem delay 100ms
tc qdisc change dev veth1 root netem delay 100ms 10ms
tc qdisc change dev veth3 root netem delay 80ms 5ms

tc qdisc add dev veth1 handle 1: root htb default 11
tc class add dev veth1 parent 1: classid 1:1 htb rate 1000Mbps
tc class add dev veth1 parent 1:1 classid 1:11 htb rate 1024Kbit
tc qdisc add dev veth1 parent 1:11 handle 10: netem delay 50ms 0ms

tc qdisc add dev veth3 handle 2: root htb default 11
tc class add dev veth3 parent 2: classid 2:1 htb rate 1000Mbps
tc class add dev veth3 parent 2:1 classid 2:11 htb rate 1024Kbit
tc qdisc add dev veth3 parent 2:11 handle 10: netem delay 50ms 0ms

tc qdisc add dev veth5 handle 4: root htb default 11
tc class add dev veth5 parent 4: classid 4:1 htb rate 1000Mbps
tc class add dev veth5 parent 4:1 classid 4:11 htb rate 1024Kbit
tc qdisc add dev veth5 parent 4:11 handle 10: netem delay 50ms 0ms

