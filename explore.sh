#!/bin/sh
# uso: explore.sh nome "botoes..."
mkdir -p $1; cd $1
DBGSTATE=1 ALARM=200 timeout 230 ../recomp_test /tmp/claude-0/work/SLUS_004.93 --bin /mnt/user-data/uploads/Dragon_Ball_GT_-_Final_Bout.bin --press START@700 $2 --press CROSS@2300 --press CROSS@2900 --cd-turbo 10 --shot 2200 --shot 2800 --shot 3400 --frames 3500 > log.txt 2>&1
echo done >> log.txt
