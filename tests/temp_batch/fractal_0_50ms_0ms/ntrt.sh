./bcex scripts/configs/mprtp6.cmds 
./bwcsv temp/pathbw_1.csv 1 2000 1200
./bwcsv temp/pathbw_2.csv 1 2000 1200
./scripts/runs/postproc/mprtp6.sh  50
