#pragma once
#include <nix/eval.hh>

#include "eval-args.hh"

class MyArgs;

namespace nix {
class AutoCloseFD;
class Bindings;
class EvalState;
template <typename T> class ref;
} // namespace nix

void worker(MyArgs &args, nix::AutoCloseFD &to, nix::AutoCloseFD &from);
