./snd_pipeline --source=FILE:foreman_cif.yuv:1:352:288:2:25/1 --codec=VP8 --stat=triggered_stat:temp/snd_packets.csv:3 --sender=MPRTP:2:1:10.0.0.6:5000:2:10.0.1.6:5002 --scheduler=MPRTPFRACTAL:MPRTP:2:1:5001:2:5003 