#!/bin/bash
set -e

PORT=8081
ROOT_DIR="./www"

mkdir -p "$ROOT_DIR"
echo "hello world" > "$ROOT_DIR/index.html"

./build/chttp --port $PORT --root "$ROOT_DIR" &
SERVER_PID=$!

sleep 1

echo "Testing GET /"
RESPONSE=$(curl -s -i http://localhost:$PORT/)
if ! echo "$RESPONSE" | grep -q "200 OK"; then
    echo "Failed to get 200 OK"
    kill $SERVER_PID
    exit 1
fi
if ! echo "$RESPONSE" | grep -q "hello world"; then
    echo "Failed to get body content"
    kill $SERVER_PID
    exit 1
fi

echo "Testing 404"
RESPONSE_404=$(curl -s -i http://localhost:$PORT/nonexistent.html)
if ! echo "$RESPONSE_404" | grep -q "404 Not Found"; then
    echo "Failed to get 404"
    kill $SERVER_PID
    exit 1
fi

echo "Testing path traversal"
RESPONSE_TRAV=$(curl -s -i --path-as-is http://localhost:$PORT/../../../../etc/passwd)
if ! echo "$RESPONSE_TRAV" | grep -q "400 Bad Request\|403 Forbidden"; then
    echo "Failed to reject path traversal"
    kill $SERVER_PID
    exit 1
fi

echo "Testing HEAD /"
RESPONSE_HEAD=$(curl -s -I http://localhost:$PORT/)
if ! echo "$RESPONSE_HEAD" | grep -q "200 OK"; then
    echo "Failed to get 200 OK for HEAD"
    kill $SERVER_PID; exit 1
fi
if echo "$RESPONSE_HEAD" | grep -q "hello world"; then
    echo "HEAD request returned a body!"
    kill $SERVER_PID; exit 1
fi

echo "Testing POST / with body"
RESPONSE_POST=$(curl -s -i -X POST -d "mydata" http://localhost:$PORT/)
if ! echo "$RESPONSE_POST" | grep -q "200 OK"; then
    echo "Failed to get 200 OK for POST"
    kill $SERVER_PID; exit 1
fi

echo "Testing Unknown Method (DELETE)"
RESPONSE_DEL=$(curl -s -i -X DELETE http://localhost:$PORT/)
if ! echo "$RESPONSE_DEL" | grep -q "405 Method Not Allowed"; then
    echo "Failed to get 405 for DELETE"
    kill $SERVER_PID; exit 1
fi
if ! echo "$RESPONSE_DEL" | grep -q "Allow: GET, HEAD, POST"; then
    echo "Failed to get Allow header for 405"
    kill $SERVER_PID; exit 1
fi

echo "Testing Keep-Alive (3 requests on 1 connection)"
RESPONSE_KA=$(curl -s -i http://localhost:$PORT/ http://localhost:$PORT/ http://localhost:$PORT/)
count=$(echo "$RESPONSE_KA" | grep -c "200 OK")
if [ "$count" -ne 3 ]; then
    echo "Keep-alive failed to serve 3 requests"
    kill $SERVER_PID; exit 1
fi

echo "Testing 10 concurrent requests"
pids=""
for i in {1..10}; do
    curl -s http://localhost:$PORT/ > /dev/null &
    pids="$pids $!"
done

for pid in $pids; do
    wait $pid
done

echo "All tests passed!"
kill $SERVER_PID
rm -rf "$ROOT_DIR"
