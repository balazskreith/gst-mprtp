./rcv_pipeline --sink=FAKESINK --codec=VP8 --stat=triggered_stat:temp/rcv_packets_3.csv:0 --plystat=triggered_stat:temp/ply_packets_3.csv:0 --playouter=MPRTPFRACTAL:MPRTP:1:1:10.0.0.1:5005 --receiver=MPRTP:1:1:5004  > temp/receiver_3.log