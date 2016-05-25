
  rm tcpstat.csv
  while true; do 
    ./scripts/mprtp1/plots.sh --srcdir logs --dstdir reports
    sleep 5
  done

  
