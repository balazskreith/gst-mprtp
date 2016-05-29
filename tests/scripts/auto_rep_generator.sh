
  rm tcpstat.csv
  while true; do 
    paste -d , logs/snd_1_ratestat.csv logs/snd_2_ratestat.csv logs/veth0.csv logs/veth2.csv logs/streamsplitter.csv > logs/snd_rates.csv
    ./scripts/mprtp1/plots.sh --srcdir logs --dstdir reports
    sleep 5
  done

  
