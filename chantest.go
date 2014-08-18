package main

import (
    "fmt"
    "time"
    "os"
)

const NTRIALS = 1000000

func threadSend(c chan string) {
    for i := 0; i < NTRIALS; i++ {
        c <- "hello"
    }
}

func threadRecv(c chan string) {
    start := time.Now()
    for i := 0; i < NTRIALS; i++ {
        <-c
    }
    fmt.Printf("elapsed (%v): %f\n", NTRIALS, time.Since(start).Seconds())
    os.Exit(0)
}

func main() {
    
    c := make(chan string, 1)
    
    go threadSend(c)
    go threadRecv(c)
    
    time.Sleep(100 * time.Second)
    
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