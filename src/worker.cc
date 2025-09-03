// doesn't exist on macOS
// IWYU pragma: no_include <bits/types/struct_rusage.h>

#include <nix/expr/eval-error.hh>
#include <nix/util/pos-idx.hh>
#include <nix/util/terminal.hh>
#include <nix/expr/attr-path.hh>
#include <nix/store/local-fs-store.hh>
#include <nix/store/globals.hh>
#include <nix/cmd/installable-flake.hh>
#include <nix/expr/value-to-json.hh>
#include <sys/resource.h>
#include <nlohmann/json.hpp>
#include <cstdio>
#include <iostream>
// NOLINTBEGIN(modernize-deprecated-headers)
// misc-include-cleaner wants this header rather than the C++ version
#include <stdlib.h>
// NOLINTEND(modernize-deprecated-headers)
#include <exception>
#include <filesystem>
#include <nix/expr/attr-set.hh>
#include <nix/cmd/common-eval-args.hh>
#include <nix/util/error.hh>
#include <nix/expr/eval.hh>
#include <nix/util/file-system.hh>
#include <nix/flake/flakeref.hh>
#include <nix/flake/flake.hh>
#include <nix/expr/get-drvs.hh>
#include <nix/util/logging.hh>
#include <nix/store/outputs-spec.hh>
#include <nix/util/ref.hh>
#include <nix/expr/symbol-table.hh>
#include <nix/util/types.hh>
#include <nix/util/util.hh>
#include <nix/expr/value.hh>
#include <nix/expr/value/context.hh>
#include <nlohmann/json_fwd.hpp>
#include <numeric>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "worker.hh"
#include "drv.hh"
#include "buffered-io.hh"
#include "eval-args.hh"
#include "store.hh"

namespace nix {
struct Expr;
} // namespace nix

namespace {
auto releaseExprTopLevelValue(nix::EvalState &state, nix::Bindings &autoArgs,
                              MyArgs &args) -> nix::Value * {
    nix::Value vTop;

    if (args.fromArgs) {
        nix::Expr *expr =
            state.parseExprFromString(args.releaseExpr, state.rootPath("."));
        state.eval(expr, vTop);
    } else {
        state.evalFile(lookupFileArg(state, args.releaseExpr), vTop);
    }

    auto *vRoot = state.allocValue();

    state.autoCallFunction(autoArgs, vTop, *vRoot);

    return vRoot;
}

auto evaluateFlake(const nix::ref<nix::EvalState> &state,
                   const std::string &releaseExpr,
                   const nix::flake::LockFlags &lockFlags) -> nix::Value * {
    auto [flakeRef, fragment, outputSpec] =
        nix::parseFlakeRefWithFragmentAndExtendedOutputsSpec(
            nix::fetchSettings, releaseExpr,
            nix::absPath(std::filesystem::path(".")));

    nix::InstallableFlake flake{{},       state,      std::move(flakeRef),
                                fragment, outputSpec, {},
                                {},       lockFlags};

    // If no fragment specified, use callFlake to get the full flake structure
    // (just like :lf in the REPL)
    if (fragment.empty()) {
        auto *value = state->allocValue();
        nix::flake::callFlake(*state, *flake.getLockedFlake(), *value);
        return value;
    }
    // Fragment specified, use normal evaluation
    return flake.toValue(*state).first;
}

auto attrPathJoin(nlohmann::json input) -> std::string {
    return std::accumulate(input.begin(), input.end(), std::string(),
                           [](const std::string &acc, std::string str) {
                               // Escape token if containing dots
                               if (str.find('.') != std::string::npos) {
                                   str = "\"" + str + "\"";
                               }
                               return acc.empty() ? str : acc + "." + str;
                           });
}
} // namespace

void worker(
    MyArgs &args,
    nix::AutoCloseFD &toParent, // NOLINT(bugprone-easily-swappable-parameters)
    nix::AutoCloseFD &fromParent) {

    auto evalStore = nix_eval_jobs::openStore(args.evalStoreUrl);
    auto state = nix::make_ref<nix::EvalState>(
        args.lookupPath, evalStore, nix::fetchSettings, nix::evalSettings);
    nix::Bindings &autoArgs = *args.getAutoArgs(*state);

    nix::Value *vRoot = [&]() {
        nix::Value *vEvaluated =
            args.flake ? evaluateFlake(state, args.releaseExpr, args.lockFlags)
                       : releaseExprTopLevelValue(*state, autoArgs, args);

        if (args.selectExpr.empty()) {
            return vEvaluated;
        }

        // Apply the provided select function
        auto *selectExpr =
            state->parseExprFromString(args.selectExpr, state->rootPath("."));

        nix::Value vSelect;
        state->eval(selectExpr, vSelect);

        nix::Value *vSelected = state->allocValue();
        state->callFunction(vSelect, *vEvaluated, *vSelected, nix::noPos);
        state->forceAttrs(
            *vSelected, nix::noPos,
            "'--select' must evaluate to an attrset (the traversal root)");

        return vSelected;
    }();

    LineReader fromReader(fromParent.release());

    while (true) {
        /* Wait for the collector to send us a job name. */
        if (tryWriteLine(toParent.get(), "next") < 0) {
            return; // main process died
        }

        auto line = fromReader.readLine();
        if (line == "exit") {
            break;
        }
        if (!nix::hasPrefix(line, "do ")) {
            std::cerr << "worker error: received invalid command '" << line
                      << "'\n";
            abort();
        }
        auto path = nlohmann::json::parse(line.substr(3));
        auto attrPathS = attrPathJoin(path);

        /* Evaluate it and send info back to the collector. */
        nlohmann::json reply =
            nlohmann::json{{"attr", attrPathS}, {"attrPath", path}};
        try {
            auto *vTmp =
                nix::findAlongAttrPath(*state, attrPathS, autoArgs, *vRoot)
                    .first;

            auto *value = state->allocValue();
            state->autoCallFunction(autoArgs, *vTmp, *value);

            if (value->type() == nix::nAttrs) {
                if (auto packageInfo =
                        nix::getDerivation(*state, *value, false)) {

                    std::optional<Constituents> maybeConstituents;
                    if (args.constituents) {
                        std::vector<std::string> constituents;
                        std::vector<std::string> namedConstituents;
                        bool globConstituents = false;
                        const auto *aggregateAttr = value->attrs()->get(
                            state->symbols.create("_hydraAggregate"));
                        if (aggregateAttr != nullptr &&
                            state->forceBool(*aggregateAttr->value,
                                             aggregateAttr->pos,
                                             "while evaluating the "
                                             "`_hydraAggregate` attribute")) {
                            const auto *constituentsAttr = value->attrs()->get(
                                state->symbols.create("constituents"));
                            if (constituentsAttr == nullptr) {
                                state
                                    ->error<nix::EvalError>(
                                        "derivation must have a ‘constituents’ "
                                        "attribute")
                                    .debugThrow();
                            }

                            nix::NixStringContext context;
                            state->coerceToString(
                                constituentsAttr->pos, *constituentsAttr->value,
                                context,
                                "while evaluating the `constituents` attribute",
                                true, false);
                            for (const auto &ctx : context) {
                                std::visit(
                                    nix::overloaded{
                                        [&](const nix::NixStringContextElem::
                                                Built &built) {
                                            constituents.push_back(
                                                built.drvPath->to_string(
                                                    *state->store));
                                        },
                                        [&](const nix::NixStringContextElem::
                                                Opaque &opaque
                                            [[maybe_unused]]) {},
                                        [&](const nix::NixStringContextElem::
                                                DrvDeep &drvDeep
                                            [[maybe_unused]]) {},
                                    },
                                    ctx.raw);
                            }

                            state->forceList(*constituentsAttr->value,
                                             constituentsAttr->pos,
                                             "while evaluating the "
                                             "`constituents` attribute");
                            auto constituents =
                                constituentsAttr->value->listView();
                            for (const auto &val : constituents) {
                                state->forceValue(*val, nix::noPos);
                                if (val->type() == nix::nString) {
                                    namedConstituents.emplace_back(
                                        val->c_str());
                                }
                            }

                            const auto *glob =
                                value->attrs()->get(state->symbols.create(
                                    "_hydraGlobConstituents"));
                            globConstituents =
                                glob != nullptr &&
                                state->forceBool(
                                    *glob->value, glob->pos,
                                    "while evaluating the "
                                    "`_hydraGlobConstituents` attribute");
                        }
                        maybeConstituents = Constituents(
                            constituents, namedConstituents, globConstituents);
                    }

                    if (!args.applyExpr.empty()) {
                        auto *applyExpr = state->parseExprFromString(
                            args.applyExpr, state->rootPath("."));

                        nix::Value vApply;
                        nix::Value vRes;

                        state->eval(applyExpr, vApply);

                        state->callFunction(vApply, *value, vRes, nix::noPos);
                        state->forceAttrs(
                            vRes, nix::noPos,
                            "apply needs to evaluate to an attrset");

                        nix::NixStringContext context;
                        std::stringstream stream;
                        nix::printValueAsJSON(*state, true, vRes, nix::noPos,
                                              stream, context);

                        reply["extraValue"] =
                            nlohmann::json::parse(stream.str());
                    }

                    auto drv = Drv(attrPathS, *state, *packageInfo, args,
                                   maybeConstituents);
                    reply.update(drv);

                    /* Register the derivation as a GC root.  !!! This
                       registers roots for jobs that we may have already
                       done. */
                    if (!args.gcRootsDir.empty() &&
                        !nix::settings.readOnlyMode && !drv.drvPath.empty()) {
                        const nix::Path root =
                            args.gcRootsDir + "/" +
                            std::string(nix::baseNameOf(drv.drvPath));
                        if (!nix::pathExists(root)) {
                            auto localStore =
                                state->store
                                    .dynamic_pointer_cast<nix::LocalFSStore>();
                            if (localStore) {
                                auto storePath =
                                    localStore->parseStorePath(drv.drvPath);
                                localStore->addPermRoot(storePath, root);
                            }
                            // If not a local store, we can't create GC roots
                        }
                    }
                } else {
                    auto attrs = nlohmann::json::array();
                    bool recurse =
                        args.forceRecurse ||
                        path.empty(); // Dont require `recurseForDerivations
                                      // = true;` for top-level attrset

                    for (auto &attr :
                         value->attrs()->lexicographicOrder(state->symbols)) {
                        const std::string_view &name =
                            state->symbols[attr->name];
                        attrs.push_back(name);

                        if (name == "recurseForDerivations" &&
                            !args.forceRecurse) {
                            const auto *attrv = value->attrs()->get(
                                state->sRecurseForDerivations);
                            recurse = state->forceBool(
                                *attrv->value, attrv->pos,
                                "while evaluating recurseForDerivations");
                        }
                    }
                    if (recurse) {
                        reply["attrs"] = std::move(attrs);
                    } else {
                        reply["attrs"] = nlohmann::json::array();
                    }
                }
            } else {
                // We ignore everything that cannot be build
                reply["attrs"] = nlohmann::json::array();
            }
        } catch (nix::EvalError &e) {
            const auto &err = e.info();
            std::ostringstream oss;
            nix::showErrorInfo(oss, err, nix::loggerSettings.showTrace.get());
            auto msg = oss.str();

            // Transmits the error we got from the previous evaluation
            // in the JSON output.
            reply["error"] = nix::filterANSIEscapes(msg, true);
            // Don't forget to print it into the STDERR log, this is
            // what's shown in the Hydra UI.
            std::cerr << msg << "\n";
        } catch (
            const std::exception &e) { // FIXME: for some reason the catch block
                                       // above, doesn't trigger on macOS (?)
            const auto *msg = e.what();
            reply["error"] = nix::filterANSIEscapes(msg, true);
            std::cerr << msg << '\n';
        }

        if (tryWriteLine(toParent.get(), reply.dump()) < 0) {
            return; // main process died
        }

        /* If our RSS exceeds the maximum, exit. The collector will
           start a new process. */
        struct rusage resourceUsage = {}; // NOLINT(misc-include-cleaner)
        getrusage(RUSAGE_SELF, &resourceUsage);
        const size_t maxrss =
            resourceUsage
                .ru_maxrss; // NOLINT(cppcoreguidelines-pro-type-union-access)
        static constexpr size_t KB_TO_BYTES = 1024;
        if (maxrss > args.maxMemorySize * KB_TO_BYTES) {
            break;
        }
    }

    if (tryWriteLine(toParent.get(), "restart") < 0) {
        return; // main process died
    };
}
