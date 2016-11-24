ntrt -cscripts/configs/ntrt_rcv_meas.ini -t310 &
iperf -s -p 1234 &
./receiver --source=FILE:foreman_cif.yuv:1:352:288:2:25/1 --codec=VP8 --sender=MPRTP:1:1:10.0.0.6:5000 --scheduler=MPRTPFRACTAL:MPRTP:1:1:5001 --stat=100:1000:1:triggered_stat --receiver=MPRTP:1:1:5000 --sink=AUTOVIDEO --playouter=MPRTPFRACTAL:MPRTP:1:1:10.0.0.1:5001 