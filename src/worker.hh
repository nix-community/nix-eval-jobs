#pragma once

#include "eval-args.hh"

class MyArgs;

namespace nix {
class AutoCloseFD;
class Bindings;
class EvalState;
template <typename T> class ref;
} // namespace nix

void worker(MyArgs &args, nix::AutoCloseFD &toParent,
            nix::AutoCloseFD &fromParent);
