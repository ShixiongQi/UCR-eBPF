package main


/*
#cgo LDFLAGS: -ldl
#include <stdlib.h>
#include <stdio.h>
#include <dlfcn.h>

int my_client_init(void* f, char* interface_name, int bpf_key)
{
  int (*client_init)(char*, int);
  client_init = (int (*)(char*, int))f;
  return client_init(interface_name, bpf_key);
}

void my_client_send(void* f)
{
  void (*client_send)();
  client_send = (void (*)())f;
  client_send();
}
*/
import "C"

import (
  "fmt"
  "log"
  "net/http"
  "os"
  "unsafe"
)

var client_send unsafe.Pointer

func handler(w http.ResponseWriter, r *http.Request) {
  log.Print("helloworld: received a request")
  C.my_client_send(client_send)
  target := os.Getenv("TARGET")
  if target == "" {
    target = "World"
  }
  fmt.Fprintf(w, "Hello %s!\n", target)
}

func main() {
  log.Print("loading bpf client module")
  handle := C.dlopen(C.CString("./libbpfclient.so"), C.RTLD_LAZY)
  client_init := C.dlsym(handle, C.CString("client_init"))
  fmt.Printf("client_init is at %p\n", client_init)
  client_send = C.dlsym(handle, C.CString("client_send"))
  fmt.Printf("client_send is at %p\n", client_send)

  C.my_client_init(client_init, C.CString("test"), C.int(0))

  log.Print("helloworld: starting server...")

  http.HandleFunc("/", handler)

  port := os.Getenv("PORT")
  if port == "" {
    port = "8080"
  }

  log.Printf("helloworld: listening on port %s", port)
  log.Fatal(http.ListenAndServe(fmt.Sprintf(":%s", port), nil))
}