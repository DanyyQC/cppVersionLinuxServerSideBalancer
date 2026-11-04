# Terminal 1 & 2: backends
python3 -m http.server 9001
python3 -m http.server 9002

# Terminal 3: LB
./lb

# Terminal 4: round-robin
curl http://localhost:8080
curl http://localhost:8080
