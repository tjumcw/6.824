#!/bin/bash
if (($1 == 1))
then `g++ server.cpp -o ser -lzmq -pthread -g`
fi

if (($1 == 2))
then `g++ client.cpp -o cli -lzmq -pthread`
fi

fifos=$(echo fifo*)

for fifo in ${fifos}
do
    #printf "fifo name is %s\n" $fifo
    `unlink $fifo &>null`
    `rm $fifo &>null`
    
done

rm per* &>null
rm snap* &>null