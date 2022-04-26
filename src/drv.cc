#include <nix/eval.hh>
#include <nix/get-drvs.hh>

#include <nlohmann/json.hpp>

using namespace nix;

namespace nix_eval_jobs {

Drv::Drv (EvalState & state, DrvInfo & drvInfo, bool meta) {
    if (drvInfo.querySystem() == "unknown")
        throw EvalError("derivation must have a 'system' attribute");

    auto localStore = state.store.dynamic_pointer_cast<LocalFSStore>();

    for (auto out : drvInfo.queryOutputs(true)) {
        if (out.second)
            outputs[out.first] = localStore->printStorePath(*out.second);

    }

    if (meta) {
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

}
