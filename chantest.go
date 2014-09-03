package main

import (
    "fmt"
    "time"
    "os"
    "runtime"
)

const NTRIALS = 1000000

func thread(c chan string) {
    start := time.Now()
    for i := 0; i < NTRIALS; i++ {
        select {
        case <-c:
        case c<-"hello":
        }
    }
    fmt.Printf("elapsed (%v): %f\n", NTRIALS, time.Since(start).Seconds())
    os.Exit(0)
}

func threadSend(c chan string) {
    for i := 0; i < NTRIALS; i++ {
        c<-"hello"
    }
}

func threadRecv(c chan string) {
    <-c
    start := time.Now()
    for i := 1; i < NTRIALS; i++ {
        <-c
    }
    fmt.Printf("elapsed (%v): %f\n", NTRIALS, time.Since(start).Seconds())
    // os.Exit(0)
}

func main() {
    runtime.GOMAXPROCS(6)
    
    c := make(chan string, 0)
    
    // go thread(c)
    // go thread(c)
    // go thread(c)
    // go thread(c)
    // go thread(c)
    // go thread(c)
    
    go threadSend(c)
    go threadSend(c)
    go threadSend(c)
    go threadRecv(c)
    go threadRecv(c)
    go threadRecv(c)
    
    for {
        time.Sleep(999 * time.Second)
    }
    
    // a := make(chan string, 1)
    //
    // start := time.Now()
    // i := 0
    // for i = 0; i < NTRIALS; i++ {
    //     select {
    //     case <-a:
    //     case a<-"hello":
    //     }
    // }
    //
    // fmt.Printf("elapsed (%v): %f\n", i, time.Since(start).Seconds())
}