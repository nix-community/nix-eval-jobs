#include <nix/config.h>
#include <nix/local-fs-store.hh>

#include <nlohmann/json.hpp>

#include "args.hh"
#include "proc.hh"

namespace nix_eval_jobs {

struct MyArgs;

Proc::Proc(MyArgs & myArgs, const Processor & proc) {
    Pipe toPipe, fromPipe;
    toPipe.create();
    fromPipe.create();
    auto p = startProcess(
        [&,
         to{std::make_shared<AutoCloseFD>(std::move(fromPipe.writeSide))},
         from{std::make_shared<AutoCloseFD>(std::move(toPipe.readSide))}
         ]()
        {
            debug("created worker process %d", getpid());
            try {
                EvalState state(myArgs.searchPath, openStore());
                Bindings & autoArgs = *myArgs.getAutoArgs(state);
                proc(myArgs, state, autoArgs, *to, *from);
            } catch (Error & e) {
                nlohmann::json err;
                auto msg = e.msg();
                err["error"] = filterANSIEscapes(msg, true);
                printError(msg);
                writeLine(to->get(), err.dump());
                // Don't forget to print it into the STDERR log, this is
                // what's shown in the Hydra UI.
                writeLine(to->get(), "restart");
            }
        },
        ProcessOptions { .allowVfork = false });

    to = std::move(toPipe.writeSide);
    from = std::move(fromPipe.readSide);
    pid = p;
}

}
