
  rm tcpstat.csv
  while true; do 
    ./scripts/rmcat6/plots.sh --srcdir logs --dstdir reports
    #./scripts/rmcat6/stats.sh --srcdir logs --dst reports/stats.csv
    sleep 5
  done

  
