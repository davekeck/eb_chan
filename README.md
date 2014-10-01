# eb_chan

`eb_chan` is a self-contained C library that implements the [CSP](http://en.wikipedia.org/wiki/Communicating_sequential_processes) primitive called a *channel*. A channel is a communication primitive that permits data to be sent between concurrent processes.

`eb_chan` is modeled closely after the channel implementation in the [Go](http://golang.org) programming language. `eb_chan` provides the full range of functionality of Go channels, along with some semantic improvements. (See the [*Semantic Differences with Go Channels*](#semantic-differences-with-go-channels) section for more information.)

This project also includes an Objective-C class, `EBChannel`, that wraps the C library and allows for a more convenient `select`-statement syntax via Objective-C blocks. (See the [*Multiplexing*](#Multiplexing) examples below.)

## Supported Platforms

`eb_chan` supports Mac OS X, iOS, and Linux.

## Integration

`eb_chan` is provided as a single header and implementation file which depend on the targeted language:

- To integrate the **C library**, add `dist/eb_chan.h` and `dist/eb_chan.c` to your project.
- To integrate the **Objective-C library**, add `dist/EBChannel.h` and `dist/EBChannel.m` to your project.

## Code Examples

#### Create Channel
> 
##### Go
```go
c := make(chan string, 0)
```
##### C
```c
eb_chan c = eb_chan_create(0);
```
##### Obj-C
```objc
EBChannel *c = [[EBChannel alloc] initWithBufferCapacity: 0];
```





#### Send
> 
##### Go
```go
// Blocking
c <- "hello"
>
// Non-blocking
select {
case c <- "hello":
  fmt.Println("Sent")
default:
  fmt.Println("Not sent")
}
```
>
##### C
```c
// Blocking
eb_chan_send(c, "hello");
>
// Non-blocking
eb_chan_res r = eb_chan_try_send(c, "hello");
if (r == eb_chan_res_ok) {
  printf("Sent\n");
} else {
  printf("Not sent\n");
}
```
>
##### Obj-C
```objc
// Blocking
[c send: @"hello"];
>
// Non-blocking
EBChannelResult r = [c trySend: @"hello"];
if (r == EBChannelResultOK) {
  NSLog(@"Sent\n");
} else {
  NSLog(@"Not sent\n");
}
```







#### Receive
> 
##### Go
```go
// Blocking
var x string
x = <-c
>
// Non-blocking
var x string
select {
case x = <-c:
  fmt.Println("Received:", x)
default:
  fmt.Println("Not received")
}
```
##### C
```c
// Blocking
char *x;
eb_chan_recv(c, &x);
>
// Non-blocking
char *x;
eb_chan_res r = eb_chan_try_recv(c, &x);
if (r == eb_chan_res_ok) {
  printf("Received: %s\n", x);
} else {
  printf("Not received\n");
}
```
##### Obj-C
```objc
// Blocking
[c recv: @"hello"];
>
// Non-blocking
id x;
EBChannelResult r = [c tryRecv: &x];
if (r == EBChannelResultOK) {
  NSLog(@"Received: %@\n", x);
} else {
  NSLog(@"Not received\n");
}
```












#### Multiplexing
> 
##### Go
```go
// Blocking
select {
case a <- "hello":
  fmt.Println("Sent on channel a")
case x := <-b:
  fmt.Println("Received on channel b:", x)
}
>
// Non-blocking
select {
case a <- "hello":
  fmt.Println("Sent on channel a")
case x := <-b:
  fmt.Println("Received on channel b:", x)
default:
  fmt.Println("Nothing")
}
```
##### C
```c
// Blocking
eb_chan_op senda = eb_chan_op_send(a, "hello");
eb_chan_op recvb = eb_chan_op_recv(b);
eb_chan_op *r = eb_chan_select(eb_nsec_forever, &senda, &recvb);
if (r == &senda) {
  printf("Sent on channel a\n");
} else if (r == &recvb) {
  printf("Received on channel b: %s\n", recvb.val);
}
>
// Non-blocking
eb_chan_op senda = eb_chan_op_send(a, "hello");
eb_chan_op recvb = eb_chan_op_recv(b);
eb_chan_op *r = eb_chan_select(eb_nsec_forever, &senda, &recvb);
if (r == &senda) {
  printf("Sent on channel a\n");
} else if (r == &recvb) {
  printf("Received on channel b: %s\n", recvb.val);
} else if (r == NULL) {
  printf("Nothing\n");
}
```
##### Obj-C
```objc
// Blocking
EBChannelOp *r = [EBChannel select: -1 ops: @[
  [a send: @"hello"], ^{
    NSLog(@"Sent on channel a");
  },
> 
  [b recv], ^(EBChannelRes result, id obj){
    NSLog(@"Received on channel b: %@", obj);
  },
]];
>
// Non-blocking
EBChannelOp *r = [EBChannel select: 0 ops: @[
  [a send: @"hello"], ^{
    NSLog(@"Sent on channel a");
  },
> 
  [b recv], ^(EBChannelRes result, id obj){
    NSLog(@"Received on channel b: %@", obj);
  },
> 
  [EBChannel defaultOp], ^{
    NSLog(@"Nothing");
  },
]];
```







#### Close Channel
> 
##### Go
```go
close(c)
```
##### C
```c
eb_chan_close(c);
```
##### Obj-C
```objc
[c close];
```




## Implementation Details

Two major goals of `eb_chan` are to maximize throughput performance and minimize resource consumption.

To maximize throughput, the implementation avoids system calls as much as possible. The implementation therefore includes a fast-path for both sending and receiving data, which involves merely acquiring a spinlock and modifying a structure. If an operation couldn't be performed on the channel after a certain number of attempts, the thread is put to sleep (if the caller allows blocking), until another thread signals the sleeping thread to try again.

To minimize resource consumption, the implementation avoids using scarce resources such as file-descriptor-based primitives (particularly UNIX pipes). For thread sleeping/waking, the implementation uses Mach semaphores (`semaphore_t`) on Darwin, and POSIX semaphores (`sem_t`) on Linux -- both of which, in the author's testing, can be allocated on the order of 10^5 without hitting resource limits.

## Semantic Differences with Go Channels

There are three semantic differences between `eb_chan` and Go channels:

1. `eb_chan` returns an error when sending on a closed channel, instead of panicking/crashing;
2. `eb_chan` returns an error when closing an already-closed channel, instead of panicking/crashing;
3. `eb_chan` `select` calls support timeouts directly, instead of having to create e.g. an extra timeout channel to signal the `select` call.

## Testing

The `eb_chan` test cases reside in the `test/` directory, and consist of the [channel test cases from the Go project](https://code.google.com/p/go/source/browse/#hg%2Ftest%2Fchan), which were manually converted from Go to C.

To run every test case, simply execute the following from the `eb_chan` repository:
```
$ cd test
$ ./test *.c
```

The expected output looks like:
```
Wrote:
  ../dist/eb_chan.h
  ../dist/eb_chan.c
chan.c: success
chan1.c: success
chancap.c: success
...
```
