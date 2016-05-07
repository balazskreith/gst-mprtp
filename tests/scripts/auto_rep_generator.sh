
  while true; do 
    ./scripts/rmcat5/plots.sh --srcdir logs --dstdir reports
    ./scripts/rmcat5/report.sh --srcdir reports --author logs/author.txt --dst report.tex
    ./scripts/pdflatex.sh report.tex

    mv report.pdf reports/report.pdf
    sleep 5
  done

  
