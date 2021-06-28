#!/bin/bash

# Basically a remote call to the .sh files on the other services with some waiting on the middle

sleep 2m

# Caterham
echo "CATERHAM"
ssh -t 192.168.20.16 '/root/sntrippled/scripts/runAgain.sh' &

echo "waiting"
sleep 2m

# Ligier
echo "LIGIER"
ssh -t 192.168.20.15 '/root/sntrippled/scripts/runAgain.sh' &

echo "waiting"
sleep 2m


# Minardi
echo "MINARDI"
ssh -t 192.168.20.11 '/root/sntrippled/scripts/runAgain.sh' &
