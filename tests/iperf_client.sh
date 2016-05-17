
iperf -c 10.0.0.2 -p 1234 -F scripts/rmcat7/file1
sleep 0 
iperf -c 10.0.0.2 -p 1234 -F scripts/rmcat7/file2
sleep 0 
iperf -c 10.0.0.2 -p 1234 -F scripts/rmcat7/file3
sleep 1 
iperf -c 10.0.0.2 -p 1234 -F scripts/rmcat7/file4
sleep 10 
iperf -c 10.0.0.2 -p 1234 -F scripts/rmcat7/file5
sleep 2 
iperf -c 10.0.0.2 -p 1234 -F scripts/rmcat7/file6
sleep 9 
iperf -c 10.0.0.2 -p 1234 -F scripts/rmcat7/file7
sleep 1 
iperf -c 10.0.0.2 -p 1234 -F scripts/rmcat7/file8
sleep 12 
iperf -c 10.0.0.2 -p 1234 -F scripts/rmcat7/file9
sleep 11 
iperf -c 10.0.0.2 -p 1234 -F scripts/rmcat7/file10
sleep 19 
