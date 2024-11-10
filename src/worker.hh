#pragma once
#include <nix/eval.hh>
#include <memory>

#include "eval-args.hh"

class MyArgs;

namespace nix {
class AutoCloseFD;
class Bindings;
class EvalState;
template <typename T> class ref;
} // namespace nix

struct Channel {
    std::shared_ptr<nix::AutoCloseFD> from, to;
};

void worker(nix::ref<nix::EvalState> state, nix::Bindings &autoArgs, Channel channel, MyArgs &args);
