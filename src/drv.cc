#include <nix/config.h> // IWYU pragma: keep

#include <nix/path-with-outputs.hh>
#include <nix/store-api.hh>
#include <nix/local-fs-store.hh>
#include <nix/value-to-json.hh>
#include <nix/derivations.hh>
#include <nix/get-drvs.hh>
#include <stdint.h>
#include <nix/derived-path-map.hh>
#include <nix/eval.hh>
#include <nix/get-drvs.hh>
#include <nix/nixexpr.hh>
#include <nlohmann/detail/json_ref.hpp>
#include <nix/path.hh>
#include <nix/ref.hh>
#include <nix/value/context.hh>
#include <exception>
#include <sstream>
#include <utility>
#include <vector>

#include "drv.hh"
#include "eval-args.hh"

static Drv::CacheStatus
queryCacheStatus(nix::Store &store,
                 std::map<std::string, std::string> &outputs) {
    uint64_t downloadSize, narSize;
    nix::StorePathSet willBuild, willSubstitute, unknown;

    std::vector<nix::StorePathWithOutputs> paths;
    for (auto const &[key, val] : outputs) {
        paths.push_back(followLinksToStorePathWithOutputs(store, val));
    }

    store.queryMissing(toDerivedPaths(paths), willBuild, willSubstitute,
                       unknown, downloadSize, narSize);
    if (willBuild.empty() && unknown.empty()) {
        return Drv::CacheStatus::NotBuild;
    } else if (willSubstitute.empty()) {
        return Drv::CacheStatus::Local;
    } else {
        return Drv::CacheStatus::Cached;
    }
}

/* The fields of a derivation that are printed in json form */
Drv::Drv(std::string &attrPath, nix::EvalState &state,
         nix::PackageInfo &packageInfo, MyArgs &args) {

    auto localStore = state.store.dynamic_pointer_cast<nix::LocalFSStore>();

    try {
        for (auto out : packageInfo.queryOutputs(true)) {
            if (out.second)
                outputs[out.first] = localStore->printStorePath(*out.second);
        }
    } catch (const std::exception &e) {
        state
            .error<nix::EvalError>(
                "derivation '%s' does not have valid outputs: %s", attrPath,
                e.what())
            .debugThrow();
    }

    if (args.meta) {
        nlohmann::json meta_;
        for (auto &metaName : packageInfo.queryMetaNames()) {
            nix::NixStringContext context;
            std::stringstream ss;

            auto metaValue = packageInfo.queryMeta(metaName);
            // Skip non-serialisable types
            // TODO: Fix serialisation of derivations to store paths
            if (metaValue == 0) {
                continue;
            }

            nix::printValueAsJSON(state, true, *metaValue, nix::noPos, ss,
                                  context);

            meta_[metaName] = nlohmann::json::parse(ss.str());
        }
        meta = meta_;
    }
    if (args.checkCacheStatus) {
        cacheStatus = queryCacheStatus(*localStore, outputs);
    } else {
        cacheStatus = Drv::CacheStatus::Unknown;
    }

    drvPath = localStore->printStorePath(packageInfo.requireDrvPath());

    auto drv = localStore->readDerivation(packageInfo.requireDrvPath());
    for (const auto &[inputDrvPath, inputNode] : drv.inputDrvs.map) {
        std::set<std::string> inputDrvOutputs;
        for (auto &outputName : inputNode.value) {
            inputDrvOutputs.insert(outputName);
        }
        inputDrvs[localStore->printStorePath(inputDrvPath)] = inputDrvOutputs;
    }
    name = packageInfo.queryName();
    system = drv.platform;
}

void to_json(nlohmann::json &json, const Drv &drv) {
    json = nlohmann::json{{"name", drv.name},
                          {"system", drv.system},
                          {"drvPath", drv.drvPath},
                          {"outputs", drv.outputs},
                          {"inputDrvs", drv.inputDrvs}};

    if (drv.meta.has_value()) {
        json["meta"] = drv.meta.value();
    }

    if (drv.cacheStatus != Drv::CacheStatus::Unknown) {
        // Deprecated field
        json["isCached"] = drv.cacheStatus == Drv::CacheStatus::Cached ||
                           drv.cacheStatus == Drv::CacheStatus::Local;

        json["cacheStatus"] =
            drv.cacheStatus == Drv::CacheStatus::Cached  ? "cached"
            : drv.cacheStatus == Drv::CacheStatus::Local ? "local"
                                                         : "notBuild";
    }
}
