# Chat Replica

Basic chat server and client.
The `server.c` was an attempt to reverse engineer a basic chat server hosted online using the provided `client` along with Wireshark.
The `server.c` successfully retains all major features of the original server.

## Usage
Use the provided `Makefile` to make the `server.run` program.
```
./server.run -p <Number>
```
`Number` - Replace with desired port number for server to be hosted on.
