./bcex scripts/configs/mprtp1.cmds 
./bwcsv temp/pathbw_1.csv 4 1000 200 2500 200 600 400 1000 400
./bwcsv temp/pathbw_2.csv 4 1000 400 2500 200 600 400 1000 200
./scripts/runs/postproc/mprtp1.sh  50
