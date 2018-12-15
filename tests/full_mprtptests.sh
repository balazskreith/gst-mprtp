#!/bin/bash
DELAY_TIME=5
INTERVAL_TIME=60

#around: RUNS*(120 + 60)
# RUNS=20
# TEST_TIME=3600

RUNS=5
TEST_TIME=1000

# RUNS=3
# TEST_TIME=600

BASE_DIR="temp_super"

echo "MPRTP 2 tests"
TEST="mprtp2"
rm -r $BASE_DIR/$TEST/*
COMMAND="sudo python3 tester/main.py $TEST -r $RUNS -u 1"
LOGFILE="$BASE_DIR/$TEST/test_runs_u1.log"
./timeout.sh -t $TEST_TIME -i $INTERVAL_TIME -d $DELAY_TIME $COMMAND &> $LOGFILE
COMMAND="sudo python3 tester/main.py $TEST -r $RUNS -u 2"
LOGFILE="$BASE_DIR/$TEST/test_runs_u2.log"
./timeout.sh -t $TEST_TIME -i $INTERVAL_TIME -d $DELAY_TIME $COMMAND &> $LOGFILE
COMMAND="sudo python3 tester/main.py $TEST -r $RUNS -u 3"
LOGFILE="$BASE_DIR/$TEST/test_runs_u3.log"
./timeout.sh -t $TEST_TIME -i $INTERVAL_TIME -d $DELAY_TIME $COMMAND &> $LOGFILE
COMMAND="sudo python3 tester/main.py $TEST -r $RUNS -u 5"
LOGFILE="$BASE_DIR/$TEST/test_runs_u5.log"
./timeout.sh -t $TEST_TIME -i $INTERVAL_TIME -d $DELAY_TIME $COMMAND &> $LOGFILE

echo "MPRTP 4 tests"
TEST="mprtp4"
rm -r $BASE_DIR/$TEST/*
COMMAND="sudo python3 tester/main.py $TEST -r $RUNS -u 1"
LOGFILE="$BASE_DIR/$TEST/test_runs_u1.log"
./timeout.sh -t $TEST_TIME -i $INTERVAL_TIME -d $DELAY_TIME $COMMAND &> $LOGFILE
COMMAND="sudo python3 tester/main.py $TEST -r $RUNS -u 2"
LOGFILE="$BASE_DIR/$TEST/test_runs_u2.log"
./timeout.sh -t $TEST_TIME -i $INTERVAL_TIME -d $DELAY_TIME $COMMAND &> $LOGFILE

echo "MPRTP 5 tests"
TEST="mprtp5"
rm -r $BASE_DIR/$TEST/*
COMMAND="sudo python3 tester/main.py $TEST -r $RUNS"
LOGFILE="$BASE_DIR/$TEST/test_runs.log"
./timeout.sh -t $TEST_TIME -i $INTERVAL_TIME -d $DELAY_TIME $COMMAND &> $LOGFILE

echo "MPRTP 7 tests"
TEST="mprtp7"
rm -r $BASE_DIR/$TEST/*
COMMAND="sudo python3 tester/main.py $TEST -r $RUNS"
LOGFILE="$BASE_DIR/$TEST/test_runs.log"
./timeout.sh -t $TEST_TIME -i $INTERVAL_TIME -d $DELAY_TIME $COMMAND &> $LOGFILE

echo "MPRTP 10 tests"
TEST="mprtp10"
rm -r $BASE_DIR/$TEST/*
COMMAND="sudo python3 tester/main.py $TEST -r $RUNS"
LOGFILE="$BASE_DIR/$TEST/test_runs.log"
./timeout.sh -t $TEST_TIME -i $INTERVAL_TIME -d $DELAY_TIME $COMMAND &> $LOGFILE

echo "MPRTP 11 tests"
TEST="mprtp11"
rm -r $BASE_DIR/$TEST/*
COMMAND="sudo python3 tester/main.py $TEST -r $RUNS"
LOGFILE="$BASE_DIR/$TEST/test_runs.log"
./timeout.sh -t $TEST_TIME -i $INTERVAL_TIME -d $DELAY_TIME $COMMAND &> $LOGFILE
