#include <nix/util.hh>
#include <nix/eval.hh>

using namespace nix;

namespace nix_eval_jobs {

struct MyArgs;

typedef std::function<void(MyArgs & myArgs, EvalState & state, Bindings & autoArgs, AutoCloseFD & to, AutoCloseFD & from)>
    Processor;

/* Auto-cleanup of fork's process and fds. */
struct Proc {
    AutoCloseFD to, from;
    Pid pid;
    Proc(MyArgs & myArgs, const Processor & proc);
    ~Proc() { }
};

}
