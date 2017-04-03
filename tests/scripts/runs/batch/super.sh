#!/bin/bash
#TEST="test7"
TEST="rmcat5"
 ./scripts/runs/batch/$TEST.sh FRACTaL 50 15
./scripts/runs/batch/$TEST.sh FRACTaL 100 15
./scripts/runs/batch/$TEST.sh FRACTaL 300 15
 ./scripts/runs/batch/$TEST.sh SCReAM 50 15
 ./scripts/runs/batch/$TEST.sh SCReAM 100 15
 ./scripts/runs/batch/$TEST.sh SCReAM 300 15
