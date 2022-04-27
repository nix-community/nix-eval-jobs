#include <nix/util.hh>
#include <nix/eval.hh>

using namespace nix;

namespace nix_eval_jobs {

typedef std::function<void(EvalState & state, Bindings & autoArgs, AutoCloseFD & to, AutoCloseFD & from)>
    Processor;

/* Auto-cleanup of fork's process and fds. */
struct Proc {
    AutoCloseFD to, from;
    Pid pid;
    Proc(const Processor & proc);
    ~Proc() { }
};

}
