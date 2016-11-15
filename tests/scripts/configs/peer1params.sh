#!/bin/bash
echo -n " "

echo -n "--source=FILE:foreman_cif.yuv:1:352:288:2:25/1 "
echo -n "--codec=VP8 "
echo -n "--sender=MPRTP:1:1:10.0.0.6:5000 "
echo -n "--scheduler=MPRTPFRACTAL:MPRTP:1:1:5001 "
echo -n "--stat=100:1000:1:triggered_stat "
    
echo -n "--receiver=MPRTP:1:1:5000 "
echo -n "--sink=AUTOVIDEO "
echo -n "--playouter=MPRTPFRACTAL:MPRTP:1:1:10.0.0.1:5001 "
