
  while true; do 
    ./scripts/rmcat3/plots.sh --srcdir logs --dstdir reports
    ./scripts/rmcat3/stats.sh --srcdir logs --dst reports/stats.csv
    ./scripts/rmcat3/report.sh --srcdir reports --author logs/author.txt --dst report.tex
    ./scripts/pdflatex.sh report.tex

    mv report.pdf reports/report.pdf
    sleep 5
  done

  
