#include "output-stream-lock.hh"
#include <iostream>

auto getCoutLock() -> OutputStreamLock & {
    static OutputStreamLock coutLock(std::cout);
    return coutLock;
}