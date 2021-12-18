cmake_minimum_required(VERSION 3.18)
project(serverless_prototype)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(CMAKE_CXX_COMPILER              "/usr/bin/clang++")
SET (CMAKE_CXX_FLAGS                "-Wall")
SET (CMAKE_CXX_FLAGS_DEBUG          "-g")
SET (CMAKE_CXX_FLAGS_MINSIZEREL     "-Os -DNDEBUG")
SET (CMAKE_CXX_FLAGS_RELEASE        "-O4 -DNDEBUG")
SET (CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g")

SET (CMAKE_AR      "/usr/bin/llvm-ar")
SET (CMAKE_LINKER  "/usr/bin/llvm-ld")
SET (CMAKE_NM      "/usr/bin/llvm-nm")
SET (CMAKE_OBJDUMP "/usr/bin/llvm-objdump")
SET (CMAKE_RANLIB  "/usr/bin/llvm-ranlib")

add_executable(gateway GatewayMain.cpp shared_includes.h queue.h config.h util.cpp util.h nanotime.cpp nanotime.h Server.cpp Server.h)
add_executable(fun FunMain.cpp shared_includes.h config.h util.cpp util.h nanotime.cpp nanotime.h Client.cpp Client.h)

target_include_directories(gateway PRIVATE ${CMAKE_SOURCE_DIR}/../../rpclib/include)
target_include_directories(fun PRIVATE ${CMAKE_SOURCE_DIR}/../../rpclib/include)

target_link_libraries(gateway ${CMAKE_SOURCE_DIR}/../../rpclib/build/librpc.a)
target_link_libraries(fun ${CMAKE_SOURCE_DIR}/../../rpclib/build/librpc.a)

target_link_libraries(gateway pthread)
target_link_libraries(gateway bpf)
target_link_libraries(gateway rt)

target_link_libraries(fun pthread)
target_link_libraries(fun bpf)
target_link_libraries(fun rt)

add_custom_target(sk_msg_kern ALL
        COMMAND clang -O2 -emit-llvm -c ${CMAKE_SOURCE_DIR}/sk_msg_kern.c -o - | llc -march=bpf -filetype=obj -o sk_msg_kern.o
        DEPENDS sk_msg_kern.c
        VERBATIM)

add_custom_target(rx_kern ALL
        COMMAND clang -O2 -emit-llvm -c ${CMAKE_SOURCE_DIR}/rx_kern.c -o - | llc -march=bpf -filetype=obj -o rx_kern.o
        DEPENDS rx_kern.c
        VERBATIM)
