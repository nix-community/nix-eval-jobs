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
                 std::map<std::string, std::optional<std::string>> &outputs) {
    uint64_t downloadSize, narSize;
    nix::StorePathSet willBuild, willSubstitute, unknown;

    std::vector<nix::StorePathWithOutputs> paths;
    for (auto const &[key, val] : outputs) {
        if (val) {
            paths.push_back(followLinksToStorePathWithOutputs(store, *val));
        }
    }

    store.queryMissing(toDerivedPaths(paths), willBuild, willSubstitute,
                       unknown, downloadSize, narSize);
    if (willBuild.empty() && unknown.empty()) {
        if (willSubstitute.empty()) {
            // cacheStatus is Local if:
            //  - there's nothing to build
            //  - there's nothing to substitute
            return Drv::CacheStatus::Local;
        } else {
            // cacheStatus is Cached if:
            //  - there's nothing to build
            //  - there are paths to substitute
            return Drv::CacheStatus::Cached;
        }
    } else {
        return Drv::CacheStatus::NotBuilt;
    }
}

/* The fields of a derivation that are printed in json form */
Drv::Drv(std::string &attrPath, nix::EvalState &state,
         nix::PackageInfo &packageInfo, MyArgs &args) {

    auto localStore = state.store.dynamic_pointer_cast<nix::LocalFSStore>();
    const bool hasCaDerivations =
        nix::experimentalFeatureSettings.isEnabled(nix::Xp::CaDerivations);

    try {
        // CA derivations do not have static output paths, so we have to
        // defensively not query output paths in case we encounter one.
        for (auto &[outputName, optOutputPath] :
             packageInfo.queryOutputs(!hasCaDerivations)) {
            if (optOutputPath) {
                outputs[outputName] =
                    localStore->printStorePath(*optOutputPath);
            } else {
                assert(hasCaDerivations);
                outputs[outputName] = std::nullopt;
            }
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
    std::map<std::string, nlohmann::json> outputsJson;
    for (auto &[name, optPath] : drv.outputs) {
        outputsJson[name] =
            optPath ? nlohmann::json(*optPath) : nlohmann::json(nullptr);
    }

    json = nlohmann::json{{"name", drv.name},
                          {"system", drv.system},
                          {"drvPath", drv.drvPath},
                          {"outputs", outputsJson},
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
                                                         : "notBuilt";
    }
}
