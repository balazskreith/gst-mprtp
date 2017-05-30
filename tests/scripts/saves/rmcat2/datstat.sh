#!/bin/sh


rm datstat*.csv
for dname in */ ; do
    algorithm=$(echo $dname | cut -d'_' -f1)
    delay=$(echo $dname | cut -d'_' -f3)
    
    #postprocessing
    if [ -z "$1" ]
    then
        NOTPROCESSING=1
    else
        echo "Postprocessing $dname"
        cd ../../..
        ./scripts/runs/postproc/rmcat2.sh $algorithm $delay scripts/saves/rmcat2/$dname
        cd scripts/saves/rmcat2
    fi
    
    flow1=$(paste -d, $dname/gp_avg_1.csv $dname/fec_avg_1.csv $dname/lr_1.csv $dname/ffre_1.csv $dname/nlf_1.csv $dname/qmd_avg_1.csv)
    echo $dname
    echo $flow1
    echo $flow1 >> datstat_flow1_$algorithm"_"$delay.csv
    flow2=$(paste -d, $dname/gp_avg_2.csv $dname/fec_avg_2.csv $dname/lr_2.csv $dname/ffre_2.csv $dname/nlf_2.csv $dname/qmd_avg_2.csv)
    echo $flow2
    echo $flow2 >> datstat_flow2_$algorithm"_"$delay.csv
     
done

#FOO = 1
#python - <<END
#import os
#print "foo:", os.environ['FOO']
#END
