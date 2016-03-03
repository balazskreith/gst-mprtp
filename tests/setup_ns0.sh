tc qdisc add dev veth0 root netem delay 100ms
tc qdisc add dev veth2 root netem delay 100ms
#tc qdisc add dev veth0 root tbf rate 1024kbit buffer 1600 limit 3000
tc qdisc change dev veth0 root pfifo limit 15
tc qdisc change dev veth0 root netem delay 100ms 10ms
tc qdisc change dev veth2 root netem delay 80ms 5ms

tc qdisc add dev veth0 handle 1: root htb default 11
tc class add dev veth0 parent 1: classid 1:1 htb rate 1000Mbps
tc class add dev veth0 parent 1:1 classid 1:11 htb rate 512Kbit
tc qdisc add dev veth0 parent 1:11 handle 10: netem delay 50ms 0ms

tc qdisc add dev veth2 handle 2: root htb default 21
tc class add dev veth2 parent 2: classid 2:1 htb rate 1000Mbps
tc class add dev veth2 parent 2:1 classid 2:11 htb rate 512Kbit
tc qdisc add dev veth2 parent 2:11 handle 10: netem delay 50ms 0ms

tc qdisc add dev veth4 handle 4: root htb default 41
tc class add dev veth4 parent 4: classid 4:1 htb rate 1000Mbps
tc class add dev veth4 parent 4:1 classid 4:11 htb rate 512Kbit
tc qdisc add dev veth4 parent 4:11 handle 10: netem delay 50ms 0ms


