#include <map>
#include <iostream>
#include <thread>
#include <filesystem>
#include <assert.h>

#include <nix/config.h>
#include <nix/shared.hh>
#include <nix/store-api.hh>
#include <nix/eval.hh>
#include <nix/eval-inline.hh>
#include <nix/util.hh>
#include <nix/get-drvs.hh>
#include <nix/globals.hh>
#include <nix/common-eval-args.hh>
#include <nix/flake/flakeref.hh>
#include <nix/flake/flake.hh>
#include <nix/attr-path.hh>
#include <nix/derivations.hh>
#include <nix/local-fs-store.hh>
#include <nix/logging.hh>
#include <nix/error.hh>

#include <nix/value-to-json.hh>

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/uio.h>

#include <nlohmann/json.hpp>

using namespace nix;
using namespace nlohmann;

// Safe to ignore - the args will be static.
#ifdef __GNUC__
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"
#elif __clang__
#pragma clang diagnostic ignored "-Wnon-virtual-dtor"
#endif
struct MyArgs : MixEvalArgs, MixCommonArgs {
    std::string releaseExpr;
    Path gcRootsDir;
    bool flake = false;
    bool fromArgs = false;
    bool meta = false;
    bool showTrace = false;
    bool impure = false;
    bool forceRecurse = false;
    bool checkCacheStatus = false;
    size_t nrWorkers = 1;
    size_t maxMemorySize = 4096;

    MyArgs() : MixCommonArgs("nix-eval-jobs") {
        addFlag({
            .longName = "help",
            .description = "show usage information",
            .handler = {[&]() {
                printf("USAGE: nix-eval-jobs [options] expr\n\n");
                for (const auto &[name, flag] : longFlags) {
                    if (hiddenCategories.count(flag->category)) {
                        continue;
                    }
                    printf("  --%-20s %s\n", name.c_str(),
                           flag->description.c_str());
                }
                ::exit(0);
            }},
        });

        addFlag({.longName = "impure",
                 .description = "allow impure expressions",
                 .handler = {&impure, true}});

        addFlag(
            {.longName = "force-recurse",
             .description = "force recursion (don't respect recurseIntoAttrs)",
             .handler = {&forceRecurse, true}});

        addFlag({.longName = "gc-roots-dir",
                 .description = "garbage collector roots directory",
                 .labels = {"path"},
                 .handler = {&gcRootsDir}});

        addFlag(
            {.longName = "workers",
             .description = "number of evaluate workers",
             .labels = {"workers"},
             .handler = {[=](std::string s) { nrWorkers = std::stoi(s); }}});

        addFlag({.longName = "max-memory-size",
                 .description = "maximum evaluation memory size",
                 .labels = {"size"},
                 .handler = {
                     [=](std::string s) { maxMemorySize = std::stoi(s); }}});

        addFlag({.longName = "flake",
                 .description = "build a flake",
                 .handler = {&flake, true}});

        addFlag({.longName = "meta",
                 .description = "include derivation meta field in output",
                 .handler = {&meta, true}});

        addFlag(
            {.longName = "check-cache-status",
             .description =
                 "Check if the derivations are present locally or in "
                 "any configured substituters (i.e. binary cache). The "
                 "information "
                 "will be exposed in the `isCached` field of the JSON output.",
             .handler = {&checkCacheStatus, true}});

        addFlag({.longName = "show-trace",
                 .description =
                     "print out a stack trace in case of evaluation errors",
                 .handler = {&showTrace, true}});

        addFlag({.longName = "expr",
                 .shortName = 'E',
                 .description = "treat the argument as a Nix expression",
                 .handler = {&fromArgs, true}});

        expectArg("expr", &releaseExpr);
    }
};
#ifdef __GNUC__
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"
#elif __clang__
#pragma clang diagnostic ignored "-Wnon-virtual-dtor"
#endif

static MyArgs myArgs;

static Value *releaseExprTopLevelValue(EvalState &state, Bindings &autoArgs) {
    Value vTop;

    if (myArgs.fromArgs) {
        Expr *e = state.parseExprFromString(myArgs.releaseExpr, absPath("."));
        state.eval(e, vTop);
    } else {
        state.evalFile(lookupFileArg(state, myArgs.releaseExpr), vTop);
    }

    auto vRoot = state.allocValue();

    state.autoCallFunction(autoArgs, vTop, *vRoot);

    return vRoot;
}

static Value *flakeTopLevelValue(EvalState &state, Bindings &autoArgs) {
    using namespace flake;

    auto [flakeRef, fragment] =
        parseFlakeRefWithFragment(myArgs.releaseExpr, absPath("."));

    auto vFlake = state.allocValue();

    auto lockedFlake = lockFlake(state, flakeRef,
                                 LockFlags{
                                     .updateLockFile = false,
                                     .useRegistries = false,
                                     .allowMutable = false,
                                 });

    callFlake(state, lockedFlake, *vFlake);

    auto vOutputs = vFlake->attrs->get(state.symbols.create("outputs"))->value;
    state.forceValue(*vOutputs, noPos);
    auto vTop = *vOutputs;

    if (fragment.length() > 0) {
        Bindings &bindings(*state.allocBindings(0));
        auto [nTop, pos] = findAlongAttrPath(state, fragment, bindings, vTop);
        if (!nTop)
            throw Error("error: attribute '%s' missing", nTop);
        vTop = *nTop;
    }

    auto vRoot = state.allocValue();
    state.autoCallFunction(autoArgs, vTop, *vRoot);

    return vRoot;
}

Value *topLevelValue(EvalState &state, Bindings &autoArgs) {
    return myArgs.flake ? flakeTopLevelValue(state, autoArgs)
                        : releaseExprTopLevelValue(state, autoArgs);
}

bool queryIsCached(Store &store, std::map<std::string, std::string> &outputs) {
    uint64_t downloadSize, narSize;
    StorePathSet willBuild, willSubstitute, unknown;

    std::vector<StorePathWithOutputs> paths;
    for (auto const &[key, val] : outputs) {
        paths.push_back(followLinksToStorePathWithOutputs(store, val));
    }

    store.queryMissing(toDerivedPaths(paths), willBuild, willSubstitute,
                       unknown, downloadSize, narSize);
    return willBuild.empty() && unknown.empty();
}

/* The fields of a derivation that are printed in json form */
struct Drv {
    std::string name;
    std::string system;
    std::string drvPath;
    bool isCached;
    std::map<std::string, std::string> outputs;
    std::optional<nlohmann::json> meta;

    Drv(EvalState &state, DrvInfo &drvInfo) {
        if (drvInfo.querySystem() == "unknown")
            throw EvalError("derivation must have a 'system' attribute");

        auto localStore = state.store.dynamic_pointer_cast<LocalFSStore>();

        for (auto out : drvInfo.queryOutputs(true)) {
            if (out.second)
                outputs[out.first] = localStore->printStorePath(*out.second);
        }

        if (myArgs.meta) {
            nlohmann::json meta_;
            for (auto &metaName : drvInfo.queryMetaNames()) {
                PathSet context;
                std::stringstream ss;

                auto metaValue = drvInfo.queryMeta(metaName);
                // Skip non-serialisable types
                // TODO: Fix serialisation of derivations to store paths
                if (metaValue == 0) {
                    continue;
                }

                printValueAsJSON(state, true, *metaValue, noPos, ss, context);

                meta_[metaName] = nlohmann::json::parse(ss.str());
            }
            meta = meta_;
        }
        if (myArgs.checkCacheStatus) {
            isCached = queryIsCached(*localStore, outputs);
        }

        name = drvInfo.queryName();
        system = drvInfo.querySystem();
        drvPath = localStore->printStorePath(drvInfo.requireDrvPath());
    }
};

static void to_json(nlohmann::json &json, const Drv &drv) {
    json = nlohmann::json{{"name", drv.name},
                          {"system", drv.system},
                          {"drvPath", drv.drvPath},
                          {"outputs", drv.outputs}};

    if (drv.meta.has_value()) {
        json["meta"] = drv.meta.value();
    }

    if (myArgs.checkCacheStatus) {
        json["isCached"] = drv.isCached;
    }
}

// Use C's FILE here as C++ ifstream cannot be created from existing file
// descriptors
struct BufferedFile {
  FILE *bufferedFile;
  size_t bufSize;
  char* lineBuf;

  BufferedFile() : bufferedFile(nullptr), bufSize(256), lineBuf(new char[bufSize]) {}

  BufferedFile(int fd, const char* mode) : bufferedFile(fdopen(fd, mode)), bufSize(256), lineBuf(new char[bufSize]) {
      if (!bufferedFile) {
          throw SysError("cannot open socket as file");
      }
  }

  BufferedFile & operator =(BufferedFile && that) {
      close();
      bufferedFile = that.bufferedFile;
      that.bufferedFile = NULL;
      return *this;
   }

  void close() {
    if (bufferedFile != nullptr) {
      fclose(bufferedFile);
    }
    bufferedFile = nullptr;
  }

  ~BufferedFile() {
      delete lineBuf;
      close();
  }

  void writeLine(std::string_view s) {
    assert(bufferedFile != nullptr);

    while (!s.empty()) {
      const struct iovec iovec[] = {
        { .iov_base = (void*)s.data(), .iov_len = s.size() },
        { .iov_base = (void*)"\n", .iov_len = 1 },
      };

      ssize_t res = writev(fileno(bufferedFile), iovec, 2);
      if (res == -1 && errno != EINTR) {
        throw SysError("writing to file");
      }
      if ((size_t)res == s.size() + 1) {
        return;
      }
      if ((size_t)res < s.size()) {
        s.remove_prefix(res);
      }
    }
  }

  std::string_view readLine() {
    assert(bufferedFile != nullptr);

    ssize_t res = getline(&lineBuf, &bufSize, bufferedFile);
    if (res < -1) {
      // FIXME is throwing an error the best choice here?
      // In nix-eval-jobs we will probably catch the error most of the time and
      // look at the child process instead.
      throw SysError("error readling file");
    }
    return {lineBuf, (size_t)res};
  }
};

// The IPC protocol

struct Message {
  enum Type {
    exit = 0,
    restart = 1,
    next = 2,
    error = 3,
    doEval = 4,
    evalResult = 5
  } type;
  std::string value;
};

void to_json(json &j, const Message &p) {
  j = json{{"type", p.type}, {"value", p.value}};
}

void from_json(const json &j, Message &p) {
  j.at("type").get_to(p.type);
  j.at("value").get_to(p.value);
}

void writeMessage(BufferedFile &sock, Message &msg) {
  json out = msg;
  sock.writeLine(out.dump());
}

Message readMessage(BufferedFile &sock) {
  auto line = sock.readLine();
  fprintf(stderr, "%s() at %s:%d: '%s'\n", __func__, __FILE__, __LINE__, line.data());
  json v = json::parse(line);
  return v.get<Message>();
}

std::string attrPathJoin(json input) {
    return std::accumulate(input.begin(), input.end(), std::string(),
                           [](std::string ss, std::string s) {
                               // Escape token if containing dots
                               if (s.find(".") != std::string::npos) {
                                   s = "\"" + s + "\"";
                               }
                               return ss.empty() ? s : ss + "." + s;
                           });
}

static void worker(EvalState &state, Bindings &autoArgs, BufferedFile &parentSock) {
    fprintf(stderr, "%s() at %s:%d, %p %p %zu\n", __func__, __FILE__, __LINE__, parentSock.bufferedFile, parentSock.lineBuf, parentSock.bufSize);
    auto vRoot = topLevelValue(state, autoArgs);
    fprintf(stderr, "%s() at %s:%d\n", __func__, __FILE__, __LINE__);

    while (true) {
        fprintf(stderr, "%s() at %s:%d\n", __func__, __FILE__, __LINE__);
        /* Wait for the collector to send us a job name. */
        auto req = Message { Message::Type::next, ""};
        writeMessage(parentSock, req);
        fprintf(stderr, "%s() at %s:%d\n", __func__, __FILE__, __LINE__);
        auto resp = readMessage(parentSock);
        fprintf(stderr, "%s() at %s:%d\n", __func__, __FILE__, __LINE__);

        if (resp.type == Message::Type::exit) {
          break;
        }
        if (resp.type != Message::Type::doEval) {
          printError("Unexpected message type %1% received", resp.type);
          abort();
        }
        auto path = resp.value;
        auto attrPathS = attrPathJoin(path);

        debug("worker process %d at '%s'", getpid(), path);

        /* Evaluate it and send info back to the collector. */
        json reply = json{{"attr", attrPathS}, {"attrPath", path}};
        try {
            auto vTmp =
                findAlongAttrPath(state, attrPathS, autoArgs, *vRoot).first;

            auto v = state.allocValue();
            state.autoCallFunction(autoArgs, *vTmp, *v);

            if (v->type() == nAttrs) {
                if (auto drvInfo = getDerivation(state, *v, false)) {
                    auto drv = Drv(state, *drvInfo);
                    reply.update(drv);

                    /* Register the derivation as a GC root.  !!! This
                       registers roots for jobs that we may have already
                       done. */
                    if (myArgs.gcRootsDir != "") {
                        Path root = myArgs.gcRootsDir + "/" +
                                    std::string(baseNameOf(drv.drvPath));
                        if (!pathExists(root)) {
                            auto localStore =
                                state.store
                                    .dynamic_pointer_cast<LocalFSStore>();
                            auto storePath =
                                localStore->parseStorePath(drv.drvPath);
                            localStore->addPermRoot(storePath, root);
                        }
                    }
                } else {
                    auto attrs = nlohmann::json::array();
                    bool recurse =
                        myArgs.forceRecurse ||
                        path.size() == 0; // Dont require `recurseForDerivations
                                          // = true;` for top-level attrset

                    for (auto &i :
                         v->attrs->lexicographicOrder(state.symbols)) {
                        const std::string &name = state.symbols[i->name];
                        attrs.push_back(name);

                        if (name == "recurseForDerivations") {
                            auto attrv =
                                v->attrs->get(state.sRecurseForDerivations);
                            recurse =
                                state.forceBool(*attrv->value, attrv->pos);
                        }
                    }
                    if (recurse)
                        reply["attrs"] = std::move(attrs);
                    else
                        reply["attrs"] = nlohmann::json::array();
                }
            } else {
                // We ignore everything that cannot be build
                reply["attrs"] = nlohmann::json::array();
            }
        } catch (EvalError &e) {
            auto err = e.info();
            std::ostringstream oss;
            showErrorInfo(oss, err, loggerSettings.showTrace.get());
            auto msg = oss.str();

            // Transmits the error we got from the previous evaluation
            // in the JSON output.
            reply["error"] = filterANSIEscapes(msg, true);
            // Don't forget to print it into the STDERR log, this is
            // what's shown in the Hydra UI.
            printError(e.msg());
        }

        fprintf(stderr, "%s() at %s:%d\n", __func__, __FILE__, __LINE__);
        auto evalResult = Message { Message::Type::evalResult, reply.dump() };
        writeMessage(parentSock, evalResult);

        /* If our RSS exceeds the maximum, exit. The collector will
           start a new process. */
        struct rusage r;
        getrusage(RUSAGE_SELF, &r);
        if ((size_t)r.ru_maxrss > myArgs.maxMemorySize * 1024)
            break;
    }

    fprintf(stderr, "%s() at %s:%d\n", __func__, __FILE__, __LINE__);
    auto exitMsg = Message { Message::Type::evalResult, "" };
    writeMessage(parentSock, exitMsg);
}

typedef std::function<void(EvalState &state, Bindings &autoArgs, BufferedFile &parentSock)>
  Processor;

/* Auto-cleanup of fork's process and fds. */
struct Proc {
    BufferedFile sock;
    Pid pid;

    Proc(const Processor &proc) {
        int pair[2] = { -1, -1 };
        if (socketpair(AF_UNIX, SOCK_STREAM|SOCK_CLOEXEC, 0, pair) < 0) {
            throw SysError("creating socketpair for ipc");
        }
        BufferedFile parentSock(pair[0], "r+");
        BufferedFile childSock(pair[1], "r+");

        auto p = startProcess([&]() {
          debug("created worker process %d", getpid());
          parentSock.close();
          try {
            EvalState state(myArgs.searchPath, openStore(*myArgs.evalStoreUrl));
            Bindings &autoArgs = *myArgs.getAutoArgs(state);

            proc(state, autoArgs, childSock);
          } catch (Error &e) {
            nlohmann::json err;
            auto msg = e.msg();
            printError(msg);
            auto errMsg = Message { Message::Type::error, filterANSIEscapes(msg, true) };
            writeMessage(parentSock, errMsg);
            // FIXME if there is an error, than this is never processed
            //auto restartMsg = Message { Message::Type::restart, "" };
            //writeMessage(parentSock, restartMsg);
          }
        });

        sock = std::move(parentSock);
        pid = p;
    }

    ~Proc() {}
};

struct State {
  std::set<json> todo = json::array({json::array()});
  std::set<json> active;
  std::exception_ptr exc;
};

std::function<void()> collector(Sync<State> &state_,
                                std::condition_variable &wakeup) {
  return [&]() {
    try {
      std::optional<std::unique_ptr<Proc>> proc_(std::nullopt);

      while (true) {

        auto proc = proc_.has_value() ? std::move(proc_.value())
                                      : std::make_unique<Proc>(worker);

        /* Check whether the existing worker process is still there. */
        fprintf(stderr, "%s() at %s:%d\n", __func__, __FILE__, __LINE__);
        auto message = readMessage(proc->sock);
        if (message.type == Message::Type::restart) {
          proc_ = std::nullopt;
          continue;
        } else if (message.type == Message::Type::error) {
          throw Error("worker error: %s", message.value);
        }

        /* Wait for a job name to become available. */
        json attrPath;

        while (true) {
          checkInterrupt();
          auto state(state_.lock());
          if ((state->todo.empty() && state->active.empty()) || state->exc) {
            auto msg = Message { Message::Type::exit, ""};
            fprintf(stderr, "%s() at %s:%d\n", __func__, __FILE__, __LINE__);
            writeMessage(proc->sock, msg);
            return;
          }
          if (!state->todo.empty()) {
            attrPath = *state->todo.begin();
            state->todo.erase(state->todo.begin());
            state->active.insert(attrPath);
            break;
          } else
            state.wait(wakeup);
        }

        /* Tell the worker to evaluate it. */
        auto req = Message { Message::Type::doEval, attrPath.dump() };
        fprintf(stderr, "%s() at %s:%d\n", __func__, __FILE__, __LINE__);
        writeMessage(proc->sock, req);

        /* Wait for the response. */
        fprintf(stderr, "%s() at %s:%d\n", __func__, __FILE__, __LINE__);
        auto resp = readMessage(proc->sock);
        //auto response = json::parse(respString);

        /* Handle the response. */
        std::vector<json> newAttrs;
        //if (response.find("attrs") != response.end()) {
        //  for (auto &i : response["attrs"]) {
        //    json newAttr = json(response["attrPath"]);
        //    newAttr.emplace_back(i);
        //    newAttrs.push_back(newAttr);
        //  }
        //} else {
        //  auto state(state_.lock());
        //  std::cout << respString << "\n" << std::flush;
        //}

        proc_ = std::move(proc);

        /* Add newly discovered job names to the queue. */
        {
          auto state(state_.lock());
          state->active.erase(attrPath);
          for (auto p : newAttrs) {
            state->todo.insert(p);
          }
          wakeup.notify_all();
        }
      }
    } catch (...) {
      auto state(state_.lock());
      state->exc = std::current_exception();
      wakeup.notify_all();
    }
  };
}

int main(int argc, char **argv) {
  /* Prevent undeclared dependencies in the evaluation via
     $NIX_PATH. */
  unsetenv("NIX_PATH");

  /* We are doing the garbage collection by killing forks */
  setenv("GC_DONT_GC", "1", 1);

  return handleExceptions(argv[0], [&]() {
    initNix();
    initGC();

    myArgs.parseCmdline(argvToStrings(argc, argv));

    /* FIXME: The build hook in conjunction with import-from-derivation is
     * causing "unexpected EOF" during eval */
    settings.builders = "";

    /* Prevent access to paths outside of the Nix search path and
       to the environment. */
    evalSettings.restrictEval = false;

    /* When building a flake, use pure evaluation (no access to
       'getEnv', 'currentSystem' etc. */
    if (myArgs.impure) {
      evalSettings.pureEval = false;
    } else if (myArgs.flake) {
      evalSettings.pureEval = true;
    }

    if (myArgs.releaseExpr == "")
      throw UsageError("no expression specified");

    if (myArgs.gcRootsDir == "") {
      printMsg(lvlError, "warning: `--gc-roots-dir' not specified");
    } else {
      myArgs.gcRootsDir = std::filesystem::absolute(myArgs.gcRootsDir);
    }

    if (myArgs.showTrace) {
      loggerSettings.showTrace.assign(true);
    }

    Sync<State> state_;

    /* Start a collector thread per worker process. */
    std::vector<std::thread> threads;
    std::condition_variable wakeup;
    for (size_t i = 0; i < myArgs.nrWorkers; i++)
      threads.emplace_back(std::thread(collector(state_, wakeup)));

    for (auto &thread : threads)
      thread.join();

    auto state(state_.lock());

    if (state->exc)
      std::rethrow_exception(state->exc);
  });
}
