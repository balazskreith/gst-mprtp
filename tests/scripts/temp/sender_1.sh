ntrt -cscripts/configs/ntrt_snd_meas.ini -mscripts/configs/ntrt_rmcat7.cmds -t310 &
python3 scripts/runs/stcp.py scripts/temp/stcp_1.dat &
python3 scripts/runs/stcp.py scripts/temp/stcp_2.dat &
python3 scripts/runs/stcp.py scripts/temp/stcp_8.dat &
python3 scripts/runs/stcp.py scripts/temp/stcp_9.dat &
python3 scripts/runs/stcp.py scripts/temp/stcp_10.dat &
./sender --source=FILE:foreman_cif.yuv:1:352:288:2:25/1 --codec=VP8 --sender=MPRTP:1:1:10.0.0.6:5000 --scheduler=MPRTPFRACTAL:MPRTP:1:1:5001 --stat=100:1000:1:triggered_stat --receiver=MPRTP:1:1:5000 --sink=AUTOVIDEO --playouter=MPRTPFRACTAL:MPRTP:1:1:10.0.0.1:5001 