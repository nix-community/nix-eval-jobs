#include <nix/config.h>
#include <nix/eval.hh>
#include <nix/get-drvs.hh>
#include <nix/globals.hh>
#include <nix/local-fs-store.hh>
#include <nix/shared.hh>
#include <nix/value-to-json.hh>

#include "args.hh"
#include "accessor.hh"
#include "job.hh"

using namespace nix;
namespace nix_eval_jobs {

/* Job */

Drv::Drv(const Drv & drv) {
    this->name = drv.name;
    this->system = drv.system;
    this->drvPath = drv.drvPath;
    this->outputs = drv.outputs;
    this->meta = drv.meta;
}

Drv::Drv(EvalState & state, Value & v) {
    auto d = getDerivation(state, v, false);
    if (d) {
        auto drvInfo = *d;

        if (drvInfo.querySystem() == "unknown")
            throw EvalError("derivation must have a 'system' attribute");

        auto localStore = state.store.dynamic_pointer_cast<LocalFSStore>();

        for (auto out : drvInfo.queryOutputs(true)) {
            if (out.second)
                outputs[out.first] = localStore->printStorePath(*out.second);

        }

        if (myArgs.meta) {
            nlohmann::json meta_;
            for (auto & name : drvInfo.queryMetaNames()) {
                PathSet context;
                std::stringstream ss;

                auto metaValue = drvInfo.queryMeta(name);
                // Skip non-serialisable types
                // TODO: Fix serialisation of derivations to store paths
                if (metaValue == 0) {
                    continue;
                }

                printValueAsJSON(state, true, *metaValue, noPos, ss, context);

                meta_[name] = nlohmann::json::parse(ss.str());
            }
            meta = meta_;
        }

        name = drvInfo.queryName();
        system = drvInfo.querySystem();
        drvPath = localStore->printStorePath(drvInfo.requireDrvPath());
    }

    else throw TypeError("expected a Drv, got: %s", showType(v));
}

JobAttrs::JobAttrs(EvalState & state, Bindings & autoArgs, Value & vIn) {
    v = state.allocValue();
    state.autoCallFunction(autoArgs, vIn, *v);

    if (v->type() != nAttrs)
        throw TypeError("wanted a JobAttrs, got %s", showType(vIn));
}

JobList::JobList(EvalState & state, Bindings & autoArgs, Value & vIn) {
    v = state.allocValue();
    state.autoCallFunction(autoArgs, vIn, *v);

    if (v->type() != nList)
        throw TypeError("wanted a JobList, got %s", showType(vIn));
}

/* children : HasChildren -> vector<Accessor> */

std::vector<std::unique_ptr<Accessor>> JobAttrs::children() {
    std::vector<std::unique_ptr<Accessor>> children;

    for (auto & a : this->v->attrs->lexicographicOrder())
        children.push_back(std::make_unique<Name>(a->name));

    return children;
}

std::vector<std::unique_ptr<Accessor>> JobList::children() {
    std::vector<std::unique_ptr<Accessor>> children;
    unsigned long i = 0;

    #ifdef __GNUC__
    #pragma GCC diagnostic ignored "-Wunused-variable"
    #elif __clang__
    #pragma clang diagnostic ignored "-Wunused-variable"
    #endif
    for (auto & _ : v->listItems())
        children.push_back(std::make_unique<Index>(i++));
    #ifdef __GNUC__
    #pragma GCC diagnostic warning "-Wunused-variable"
    #elif __clang__
    #pragma clang diagnostic warning "-Wunused-variable"
    #endif

    return children;
}

/* eval : Job -> EvalState -> JobEvalResult */

std::unique_ptr<JobEvalResult> Drv::eval(EvalState & state) {
    /* Register the derivation as a GC root.  !!! This
       registers roots for jobs that we may have already
       done. */
    if (myArgs.gcRootsDir != "") {
        Path root = myArgs.gcRootsDir + "/" + std::string(baseNameOf(this->drvPath));
        if (!pathExists(root)) {
            auto localStore = state.store.dynamic_pointer_cast<LocalFSStore>();
            auto storePath = localStore->parseStorePath(this->drvPath);
            localStore->addPermRoot(storePath, root);
        }
    }

    return std::make_unique<Drv>(*this);
}

std::unique_ptr<JobEvalResult> JobAttrs::eval(EvalState & state) {
    return std::make_unique<JobChildren>(*this);
}

std::unique_ptr<JobEvalResult> JobList::eval(EvalState & state) {
    return std::make_unique<JobChildren>(*this);
}

/* JobEvalResult */

JobChildren::JobChildren(HasChildren & parent) {
    this->children = parent.children();
}

/* toJson : JobEvalResult -> json */

nlohmann::json JobChildren::toJson() {
    std::vector<nlohmann::json> children;

    for (auto & child : this->children)
        children.push_back(child->toJson());

    return nlohmann::json{ {"children", children } };

}

nlohmann::json Drv::toJson() {
    nlohmann::json json;

    json["name"]  = this->name;
    json["system"] = this->system;
    json["drvPath"] = this->drvPath;
    json["outputs"] = this->outputs;

    if (this->meta) json["meta"] = *this->meta;

    return json;
}

}
