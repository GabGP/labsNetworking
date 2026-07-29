#!/bin/bash
./dns_server -p 9999 -u 8.8.8.8 -t 4 &
SERVER_PID=$!
sleep 1

python3 test_dns.py

kill -SIGINT $SERVER_PID
wait $SERVER_PID 2>/dev/null
echo "Done testing."
