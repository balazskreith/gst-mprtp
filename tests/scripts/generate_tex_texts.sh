#!/bin/bash

function usage {
    echo "usage: $programname [-o|--output filename] [-c|--testcase casenum]"
    echo "	-o --output dir	determines output directory"
    echo "	-tc --testcase	determines case file"
    exit 1
}

if [  $# -le 1 ] 
then 
  usage
  exit 1
fi 

TESTCASE=0
OUTPUT="logs/tmp/"

while [[ $# > 1 ]]
do
key="$1"

case $key in
    -tc|--testcase)
    TESTCASE="$2"
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

function testcase_1 {
echo " 
In this test case the bottleneck-link capacity between the two
endpoints varies over time.  This test is designed to measure the
responsiveness of the candidate algorithm.  This test tries to
address the requirements in [I-D.ietf-rmcat-cc-requirements].

\textbf{Expected behavior}: the candidate algorithm is expected to detect the
path capacity constraint, converges to bottleneck link's capacity and
adapt the flow to avoid unwanted oscillation when the sending bit
rate is approaching the bottleneck link's capacity.  The oscillations
occur when the media flow(s) attempts to reach its maximum bit rate,
overshoots the usage of the available bottleneck capacity, to rectify
it reduces the bit rate and starts to ramp up again.

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

\textbf{Test Specific Information}\\
\begin{itemize}
\item This test uses the following one way propagation delays of 50
 ms and 100 ms.
\item This test uses bottleneck path capacity variation as listed in
 Table 1
\item When using background non-adaptive UDP traffic to induce time-
varying bottleneck for the RMCAT flow, the physical path
capacity is 4Mbps and the UDP traffic source rate changes over
time as (4-x)Mbps, where x is the bottleneck capacity specified
in Table 1
\end{itemize}

\begin{figure}
\begin{verbatim}
   +--------------------+--------------+-----------+-------------------+
   | Variation pattern  | Path         | Start     | Path capacity     |
   | index              | direction    | time      | ratio             |
   +--------------------+--------------+-----------+-------------------+
   | One                | Forward      | 0s        | 1.0               |
   | Two                | Forward      | 40s       | 2.5               |
   | Three              | Forward      | 60s       | 0.6               |
   | Four               | Forward      | 80s       | 1.0               |
   +--------------------+--------------+-----------+-------------------+

      Table 1: Path capacity variation pattern for forward direction

\end{verbatim}
\caption{Constant RTP transmission testcase topology}
\label{fig:ascii-box}
\end{figure}


" > $1
}

TESTCASEANDTOPOLOGY="$OUTPUT/TESTCASEANDTOPOLOGY.txt"

echo "generating testcase $CASENUM into $OUTPUT folder"

if [ $TESTCASE -eq 0 ]; then
  testcase_0 $TESTCASEANDTOPOLOGY
elif [ $TESTCASE -eq 1 ]; then
  testcase_1 $TESTCASEANDTOPOLOGY
fi




