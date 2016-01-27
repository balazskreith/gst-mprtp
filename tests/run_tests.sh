#!/bin/bash
./run_2s_test.sh run_veth0_const.sh run_veth2_const.sh 0
sleep 1
./run_2s_test.sh run_veth0_const.sh run_veth2_const.sh 0
sleep 1
./run_2s_test.sh run_veth0_const.sh run_veth2_const.sh 1
sleep 1
./run_2s_test.sh run_veth0_const.sh run_veth2_const.sh 1
sleep 1
./run_2s_test.sh run_veth0_const.sh run_veth2_const.sh 2
sleep 1
./run_2s_test.sh run_veth0_const.sh run_veth2_const.sh 2
sleep 1
./run_2s_test.sh run_veth0_const.sh run_veth2_const.sh 3
sleep 1
./run_2s_test.sh run_veth0_const.sh run_veth2_const.sh 3
sleep 1

./run_2s_test.sh run_veth0_cc.sh run_veth2_const.sh 0
sleep 1
./run_2s_test.sh run_veth0_cc.sh run_veth2_const.sh 0
sleep 1
./run_2s_test.sh run_veth0_cc.sh run_veth2_const.sh 1
sleep 1
./run_2s_test.sh run_veth0_cc.sh run_veth2_const.sh 1
sleep 1
./run_2s_test.sh run_veth0_cc.sh run_veth2_const.sh 2
sleep 1
./run_2s_test.sh run_veth0_cc.sh run_veth2_const.sh 2
sleep 1
./run_2s_test.sh run_veth0_cc.sh run_veth2_const.sh 3
sleep 1
./run_2s_test.sh run_veth0_cc.sh run_veth2_const.sh 3


./run_2s_test.sh run_veth0_cc.sh run_veth2_cc.sh 0
sleep 1
./run_2s_test.sh run_veth0_cc.sh run_veth2_cc.sh 0
sleep 1
./run_2s_test.sh run_veth0_cc.sh run_veth2_cc.sh 1
sleep 1
./run_2s_test.sh run_veth0_cc.sh run_veth2_cc.sh 1
sleep 1
./run_2s_test.sh run_veth0_cc.sh run_veth2_cc.sh 2
sleep 1
./run_2s_test.sh run_veth0_cc.sh run_veth2_cc.sh 2
sleep 1
./run_2s_test.sh run_veth0_cc.sh run_veth2_cc.sh 3
sleep 1
./run_2s_test.sh run_veth0_cc.sh run_veth2_cc.sh 3

pm-suspend

