#!/bin/bash
./sentinel_test > server.log 2>&1 &
SERVER_PID=$!
sleep 2
./test_complete.sh > test_output.log 2>&1
echo "Finished test_complete.sh"
kill $SERVER_PID
