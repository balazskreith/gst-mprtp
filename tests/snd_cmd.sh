set -x
tc qdisc del dev veth0 root
tc qdisc add dev veth0 handle 1: root htb
tc class add dev veth0 parent 1: classid 1:1 htb rate 3500Kbit
tc class add dev veth0 parent 1:1 classid 1:11 htb rate 3500Kbit
tc qdisc add dev veth0 parent 1:11 handle 10: tbf rate 3500Kbit burst 15400 latency 300ms minburst 1540
tc class add dev veth0 parent 1:1 classid 1:12 htb rate 3500Kbit
tc qdisc add dev veth0 parent 1:12 handle 20: tbf rate 3500Kbit burst 15400 latency 300ms minburst 1540

tc filter add dev veth0 protocol ip prio 1 u32 match ip dport 5000 0xfff0 flowid 1:11
tc filter add dev veth0 protocol ip prio 1 u32 match ip dport 5016 0xfff0 flowid 1:12
