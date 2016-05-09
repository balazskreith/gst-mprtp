
  while true; do 
    ./scripts/rmcat6/plots.sh --srcdir logs --dstdir reports
    ./scripts/rmcat6/stats.sh --srcdir logs --dst reports/stats.csv
    mv logs/ccparams_1.log reports/ccparams_1.log
    ./scripts/rmcat6/report.sh --srcdir reports --author logs/author.txt --dst report.tex
    ./scripts/pdflatex.sh report.tex

    mv report.pdf reports/report.pdf
    sleep 5
  done

  
