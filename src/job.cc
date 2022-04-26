#include <nix/eval.hh>
#include <nix/get-drvs.hh>
#include <nix/local-fs-store.hh>
#include <nix/value-to-json.hh>

#include <nlohmann/json.hpp>

using namespace nix;

namespace nix_eval_jobs {

typedef std::vector<std::unique_ptr<Accessor>> JobChildren;

std::unique_ptr<Job> getJob(EvalState & state, Bindings & autoArgs, Value & v) {
    try {
        return std::make_unique<JobDrv>(state, v);
    } catch (TypeError & _) {
        try {
            return std::make_unique<JobAttrs>(state, autoArgs, v);
        } catch (TypeError & _) {
            try {
                return std::make_unique<JobList>(state, autoArgs, v);
            } catch (TypeError & _) {
                throw TypeError("error creating job, expecting one of a derivation, an attrset or a derivation, got: %s", showType(v));
            }
        }
    }
}

JobDrv::JobDrv(EvalState & state, Value & v) {
    auto d = getDerivation(state, v, false);
    if (d) thix->drvInfo = &*d;

    else throw TypeError("expected a JobDrv, got: %s", showType(v));
}

JobDrv::JobDrv(DrvInfo * d) {
    drvInfo = d;
}

JobDrv::~JobDrv() { }

JobAttrs::JobAttrs(EvalState & state, Bindings & autoArgs, Value & vIn) {
    v = state.allocValue();
    state.autoCallFunction(autoArgs, vIn, *v);

    if (v->type() != nAttrs)
        throw TypeError("wanted a JobAttrs, got %s", showType(vIn));
}

JobAttrs::~JobAttrs() { }

JobList::JobList(EvalState & state, Bindings & autoArgs, Value & vIn) {
    v = state.allocValue();
    state.autoCallFunction(autoArgs, vIn, *v);

    if (v->type() != nList)
        throw TypeError("wanted a JobList, got %s", showType(vIn));
}

JobList::~JobList() { }

std::optional<JobChildren> JobDrv::children() {
    return std::nullopt;
}

std::optional<JobChildren> JobAttrs::children() {
    JobChildren children;

    for (auto & a : v->attrs->lexicographicOrder())
        children.push_back(std::make_unique<Name>(a->name));

    return children;
}

std::optional<JobChildren> JobList::children() {
    JobChildren children;
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

}
