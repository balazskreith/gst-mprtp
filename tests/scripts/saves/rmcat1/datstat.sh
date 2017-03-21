#!/bin/sh
rm datstat*.csv
for dname in */ ; do
    algorithm=$(echo $dname | cut -d'_' -f1)
    delay=$(echo $dname | cut -d'_' -f3)
    
    paste -d, $dname/gp_avg.csv $dname/fec_avg.csv $dname/lr.csv >> datstat_$algorithm"_"$delay.csv
     
done

#FOO = 1
#python - <<END
#import os
#print "foo:", os.environ['FOO']
#END
