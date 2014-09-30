# Summary

`eb_chan` is a self-contained C library that implements the [CSP](http://en.wikipedia.org/wiki/Communicating_sequential_processes) primitive called a *channel*. A channel is a communication primitive that permits data to be sent between concurrent processes.

`eb_chan` is modeled closely after the channel implementation in the [Go](http://golang.org) programming language. `eb_chan` intends to provide the full range of functionality of Go channels, along with some (subjective) semantic improvements. (See the [*Go Semantic Differences*](#Go Semantic Differences) section for more information.)

This project also includes an Objective-C class, `EBChannel`, that wraps the C library and allows for a more convenient `select`-statement syntax via Objective-C blocks. (See the [*Multiplexing*](#Multiplexing) examples below.)

# Supported Platforms

`eb_chan` supports Mac OS X, iOS, and Linux.

# Implementation Details

A primary goal of this project is throughput performance and low resource consumption. These two goals have several implementation implications worth mentioning:

1. To maximize throughput, the implementation avoids system calls as much as possible. The implementation therefore includes a fast-path which involves merely acquiring a spinlock and modifying a structure. If an operation couldn't be performed on the channel after a certain number of attempts, the thread is put to sleep (if the caller allows blocking), until another thread signals the sleeping thread to try again.
2. To minimize resource consumption, the implementation avoids using scarce resources such as file descriptors (particularly via UNIX pipes). For thread sleeping/waking, the implementation uses Mach semaphores (`semaphore_t`) on Darwin, and POSIX semaphores (`sem_t`) on Linux.  

# Integration

`eb_chan` is provided as a single header and implementation file which depend on the targeted language:

- To integrate the **C library**, add `dist/eb_chan.h` and `dist/eb_chan.c` to your project.
- To integrate the **Objective-C library**, add `dist/EBChannel.h` and `dist/EBChannel.m` to your project.

# Code Examples

#### Create Channel

<table>
  <tr>
    <td><b>
        Go
    </b></td>
    <td><code>
        c := make(chan T, 0)
    </code></td>
  </tr>
  <tr>
    <td><b>
        C
    </b></td>
    <td><code>
        eb_chan c = eb_chan_create(0);
    </code></td>
  </tr>
  <tr>
    <td><b>
        Objective-C
    </b></td>
    <td><code>
        EBChannel *c = [[EBChannel alloc] initWithBufferCapacity: 0];
    </code></td>
  </tr>
</table>

#### Send

<table>
  <tr>
    <td><b>
        Go
    </b></td>
    <td><code>
        c &lt;- x
    </code></td>
  </tr>
  <tr>
    <td><b>
        C
    </b></td>
    <td><code>
        eb_chan_send(c, x);
    </code></td>
  </tr>
  <tr>
    <td><b>
        Objective-C
    </b></td>
    <td><code>
        [c send: x];
    </code></td>
  </tr>
</table>

#### Receive

<table>
  <tr>
    <td><b>
        Go
    </b></td>
    <td><code>
        x := &lt;-c
    </code></td>
  </tr>
  <tr>
    <td><b>
        C
    </b></td>
    <td>
<code>
void *x;

eb_chan_recv(c, &x);
</code>
    </td>
  </tr>
  <tr>
    <td><b>
        Objective-C
    </b></td>
    <td><code>
        // receive
    </code></td>
  </tr>
</table>

#### Close Channel

<table>
  <tr>
    <td><b>
        Go
    </b></td>
    <td><code>
        close(c)
    </code></td>
  </tr>
  <tr>
    <td><b>
        C
    </b></td>
    <td><code>
        eb_chan_close(c);
    </code></td>
  </tr>
  <tr>
    <td><b>
        Objective-C
    </b></td>
    <td><code>
        [c close];
    </code></td>
  </tr>
</table>
