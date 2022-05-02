#!/bin/bash

make debug

for i in `seq 1 $1`
do
    echo "TEST (EVEN) NUMBER $i"
    ./proj2 2 4 100 100
done

for i in `seq 1 $1`
do
    echo "TEST (ODD) NUMBER $i"
    ./proj2 3 5 100 100
done

for i in `seq 1 $1`
do
    echo "TEST (ODD) NUMBER $i"
    ./proj2 5 3 100 100
done

echo "Done... if you see this message. No deadlocks happened."
