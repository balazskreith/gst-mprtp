import numpy
import sys
import os

output = sys.argv[1] # prints var1
#output = "output.dat"

for x in range(0,5) :
    idle = numpy.random.exponential(20)
    cmd = "sleep " + str(idle)
    os.system(cmd)
    size = str(int(numpy.random.uniform(100, 10240)))
    cmd = "dd if=/dev/zero of=" + output + " bs=" + str(size) + "KB  count=1"
    os.system(cmd)
    cmd = "iperf -c 10.0.0.6 -F " + output + " -p 1234"
    os.system(cmd)
 #   !dd if=/dev/zero of=$output  bs="$size"KB  count=1 > log.txt
 #   !iperf -c 127.0.0.1 -F $output -p 1234 > log.txt
