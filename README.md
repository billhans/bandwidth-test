### One-to-many bandwidth test program

Based on TCP, UDP, ncurses bandwidth speed measurement program

#### compile code
```
gcc server.c -o server -lpthread -lncurses
gcc client.c -o client -lpthread
```

#### run code
```
./server tcp double
./client tcp double
```
#### press keyboard u/d/p to toggle up/down/double mode test
#### enter a number to add the speed limit