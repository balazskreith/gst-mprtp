./bcex scripts/configs/rmcat3_1.cmds &
./bcex scripts/configs/rmcat3_2.cmds 
./bwcsv temp/pathbw_1.csv 4 2000 200 1000 200 500 200 2000 400
./bwcsv temp/pathbw_2.csv 3 2000 350 800 350 2000 300
./scripts/runs/postproc/rmcat3.sh
