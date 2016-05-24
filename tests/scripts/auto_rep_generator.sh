
  rm tcpstat.csv
  while true; do 
    ./scripts/rmcat7/plots.sh --srcdir logs --dstdir reports
    sleep 5
  done

  
