# Tests for gst-mprtp

To test the plugin a `sender` and `receiver` is built.
The `sender` and `receiver` need to run in a separate namespace.
This is done by a `tester` application.

## Compile and Run

You need to setup the testbed for the test, so please run:
```shell script
./scripts/setup_testbed.sh
```

After that you can run tests by type:

```shell script
python3 tester/main.py TEST_ARGUMENTS
```

## Tester

the `tester` is the orchastraetor, controls the test and evalute the results.

```shell script
usage: main.py [-h] [-l {50,100,150,300} [{50,100,150,300} ...]]
               [-j JITTER [JITTER ...]] [-a {FRACTaL,SCReAM}] [-r RUNS]
               [-t TARGET] [-s SOURCE] [-i SINK] [-u SUBFLOWS_NUM] [--tcp TCP]
               [--vqmt VQMT]
               {rmcat1,rmcat2,rmcat3,rmcat4,rmcat5,rmcat6,rmcat7,mprtp1,mprtp2,mprtp3,mprtp4,mprtp5,mprtp6,mprtp7,mprtp8,mprtp9,mprtp10,mprtp11}
               [{rmcat1,rmcat2,rmcat3,rmcat4,rmcat5,rmcat6,rmcat7,mprtp1,mprtp2,mprtp3,mprtp4,mprtp5,mprtp6,mprtp7,mprtp8,mprtp9,mprtp10,mprtp11} ...]

positional arguments:
  {rmcat1,rmcat2,rmcat3,rmcat4,rmcat5,rmcat6,rmcat7,mprtp1,mprtp2,mprtp3,mprtp4,mprtp5,mprtp6,mprtp7,mprtp8,mprtp9,mprtp10,mprtp11}
                        The type of the test

optional arguments:
  -h, --help            show this help message and exit
  -l {50,100,150,300} [{50,100,150,300} ...], --latency {50,100,150,300} [{50,100,150,300} ...]
                        The late ncy of the path
  -j JITTER [JITTER ...], --jitter JITTER [JITTER ...]
                        The jitter for the test
  -a {FRACTaL,SCReAM}, --algorithm {FRACTaL,SCReAM}
                        The algorithm for the test
  -r RUNS, --runs RUNS  The runtimes
  -t TARGET, --target TARGET
                        The target directory
  -s SOURCE, --source SOURCE
                        The source type format
  -i SINK, --sink SINK  The sink type format
  -u SUBFLOWS_NUM, --subflows_num SUBFLOWS_NUM
                        The number of subflows we want
  --tcp TCP             TCP Indicator
  --vqmt VQMT           Calculate metrics video quality metrics using VQMT
                        after the run
```

### Examples

```shell script
python3 tester/main.py rmcat1 -l 100 -j 10 -a FRACTaL
```

Runs a test type of RMCAT 1, in which the latency between the sender and receiver is 100ms, 
the jitter is 10ms, and the used algorithm is FRACTaL.