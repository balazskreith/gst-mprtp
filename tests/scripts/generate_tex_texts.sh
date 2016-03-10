#!/bin/bash

function usage {
    echo "usage: $programname [-o|--output filename] [-c|--testcase casenum]"
    echo "	-o --output dir	determines output directory"
    echo "	-c --testcase	determines case file"
    exit 1
}

if [  $# -le 1 ] 
then 
  usage
  exit 1
fi 

CASENUM=0
OUTPUT="logs/tmp/"

while [[ $# > 1 ]]
do
key="$1"

case $key in
    -c|--testcase)
    CASENUM="$2"
    shift # past argument
    ;;
    -o|--output)
    OUTPUT="$2"
    shift # past argument
    ;;
    --default)
    DEFAULT=YES
    ;;
    *)
            # unknown option
    ;;
esac
shift # past argument or value
done


function testcase_0 {
echo " 
In this test case the bottleneck-link capacity between the two
endpoints remains constant over time. This test is designed to measure 
the stability of the cancdidate algorithm.

\textbf{Expected behavior}: the candidate algorithm is expected to detect the
path capacity constraint, converges to bottleneck link's capacity and
adapt the flow to avoid unwanted oscillation when the sending bit
rate is approaching the bottleneck link's capacity.  

\begin{figure}
\begin{verbatim}
                            Forward -->
+---+       +-----+                               +-----+       +---+
|S1 |=======|  A  |------------------------------>|  B  |=======|R1 |
+---+       |     |<------------------------------|     |       +---+
            +-----+                               +-----+
                         <-- Backward
\end{verbatim}
\caption{Constant RTP transmission testcase topology}
\label{fig:ascii-box}
\end{figure}

" > $1
}

TESTCASEANDTOPOLOGY="$OUTPUT/TESTCASEANDTOPOLOGY.txt"

echo "generating testcase $CASENUM into $OUTPUT folder"

if [ $CASENUM -eq 0 ]; then
  testcase_0 $TESTCASEANDTOPOLOGY
fi




