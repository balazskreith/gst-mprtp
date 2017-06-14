#!/bin/bash
#TEST="test7"
mv temp_batch rmcat1
mkdir temp_batch
TEST="rmcat2"


TARGET="temp_super"
SOURCE="temp_batch"
tests=( rmcat1 rmcat2 rmcat3 rmcat4 rmcat5 rmcat6 rmcat7 )
tests=( rmcat4 rmcat5 ) 
#tests=( mprtp1 mprtp2 mprtp3 mprtp4 mprtp5 mprtp6 mprtp7 )
#tests=( rmcat1 )
#owds=( 50 100 300 )
owds=( 50 )
#algorithms=( FRACTaL SCReAM )
algorithms=( FRACTaL )
testnum=5


for test in "${tests[@]}"
do
	for algorithm in "${algorithms[@]}"
	do
		echo "------------------------------------------------------"
		echo "---------------------- TESTING -----------------------"
		echo "------------------------------------------------------"
		echo "TEST:      $test"
		echo "ALGORITHM: $algorithm"
		echo "------------------------------------------------------"
		
		if [ "$test" == "rmcat5" ] 
		then
			./scripts/runs/batch/$test.sh $algorithm 50 $testnum
			continue
		fi
		for owd in "${owds[@]}"
		do
			./scripts/runs/batch/$test.sh $algorithm $owd $testnum
		done
	done
	rm -rf "$TARGET/$test"
	mv "$SOURCE" "$TARGET/$test"
	mkdir "$SOURCE"
done

#./scripts/runs/batch/$TEST.sh FRACTaL 50 50
#./scripts/runs/batch/$TEST.sh FRACTaL 100 50
#./scripts/runs/batch/$TEST.sh FRACTaL 300 50
#./scripts/runs/batch/$TEST.sh SCReAM 50 50
#./scripts/runs/batch/$TEST.sh SCReAM 100 50
#./scripts/runs/batch/$TEST.sh SCReAM 300 50
