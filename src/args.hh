#pragma once

#include <nix/common-args.hh>
#include <nix/common-eval-args.hh>

using namespace nix;

namespace nix_eval_jobs {

typedef enum { evalAuto, evalImpure, evalPure } pureEval;

// Safe to ignore - the args will be static.
#ifdef __GNUC__
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"
#elif __clang__
#pragma clang diagnostic ignored "-Wnon-virtual-dtor"
#endif
struct MyArgs : MixEvalArgs, MixCommonArgs
{
    Path releaseExpr;
    Path gcRootsDir;
    bool flake = false;
    bool meta = false;
    bool showTrace = false;
    size_t nrWorkers = 1;
    size_t maxMemorySize = 4096;
    pureEval evalMode = evalAuto;

    MyArgs() : MixCommonArgs("nix-eval-jobs")
    {
        addFlag({
            .longName = "help",
            .description = "show usage information",
            .handler = {[&]() {
                printf("USAGE: nix-eval-jobs [options] filepath\n\n");
                printf("  <filepath> should evaluate to one of: a derivation, or a list or set of derivations.\n\n");
                for (const auto & [name, flag] : longFlags) {
                    if (hiddenCategories.count(flag->category)) {
                        continue;
                    }
                    printf("  --%-20s %s\n", name.c_str(), flag->description.c_str());
                }
                ::exit(0);
            }},
        });

        addFlag({
            .longName = "impure",
            .description = "set evaluation mode",
            .handler = {[&]() {
                evalMode = evalImpure;
            }},
        });

        addFlag({
            .longName = "gc-roots-dir",
            .description = "garbage collector roots directory",
            .labels = {"path"},
            .handler = {&gcRootsDir}
        });

        addFlag({
            .longName = "workers",
            .description = "number of evaluate workers",
            .labels = {"workers"},
            .handler = {[=](std::string s) {
                nrWorkers = std::stoi(s);
            }}
        });

        addFlag({
            .longName = "max-memory-size",
            .description = "maximum evaluation memory size",
            .labels = {"size"},
            .handler = {[=](std::string s) {
                maxMemorySize = std::stoi(s);
            }}
        });

        addFlag({
            .longName = "flake",
            .description = "build a flake",
            .handler = {&flake, true}
        });

        addFlag({
            .longName = "meta",
            .description = "include derivation meta field in output",
            .handler = {&meta, true}
        });

        addFlag({
            .longName = "show-trace",
            .description = "print out a stack trace in case of evaluation errors",
            .handler = {&showTrace, true}
        });

        expectArg("expr", &releaseExpr);
    }
};
#ifdef __GNUC__
#pragma GCC diagnostic warning "-Wnon-virtual-dtor"
#elif __clang__
#pragma clang diagnostic warning "-Wnon-virtual-dtor"
#endif

static MyArgs myArgs;

}
