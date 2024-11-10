#include <nix/config.h> // IWYU pragma: keep
#include <nix/path-with-outputs.hh>
#include <nix/store-api.hh>
#include <nix/local-fs-store.hh>
#include <nix/value-to-json.hh>
#include <nix/derivations.hh>
#include <nix/get-drvs.hh>
#include <nix/derived-path-map.hh>
#include <nix/eval.hh>
#include <nlohmann/detail/json_ref.hpp>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>
#include <nix/path.hh>
#include <nix/ref.hh>
#include <nix/value/context.hh>
#include <nix/error.hh>
#include <nix/eval-error.hh>
#include <nix/experimental-features.hh>
#include <nix/pos-idx.hh>
#include <exception>
#include <sstream>
#include <vector>
#include <memory>

#include "drv.hh"
#include "eval-args.hh"

static auto
queryCacheStatus(nix::Store &store,
                 std::map<std::string, std::optional<std::string>> &outputs)
    -> Drv::CacheStatus {
    uint64_t downloadSize;
    uint64_t narSize;
    nix::StorePathSet willBuild;
    nix::StorePathSet willSubstitute;
    nix::StorePathSet unknown;

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
        }
        // cacheStatus is Cached if:
        //  - there's nothing to build
        //  - there are paths to substitute
        return Drv::CacheStatus::Cached;
    }
    return Drv::CacheStatus::NotBuilt;
}

/* The fields of a derivation that are printed in json form */
Drv::Drv(std::string &attrPath, nix::EvalState &state,
         nix::PackageInfo &packageInfo, MyArgs &args) {

    auto localStore = state.store.dynamic_pointer_cast<nix::LocalFSStore>();

    try {
        // CA derivations do not have static output paths, so we have to
        // fallback if we encounter an error
        try {
            for (auto &[outputName, optOutputPath] :
                 packageInfo.queryOutputs(true)) {
                if (!optOutputPath) {
                    continue;
                }
                outputs[outputName] =
                    localStore->printStorePath(*optOutputPath);
            }
        } catch (const nix::UnimplementedError &e) {
            if (!nix::experimentalFeatureSettings.isEnabled(
                    nix::Xp::CaDerivations)) {
                // If we do have CA derivations enabled, we should not encounter
                // this error.
                throw;
            }
            // we are probably hitting this:
            // https://github.com/NixOS/nix/blob/39da9462e9c677026a805c5ee7ba6bb306f49c59/src/libexpr/get-drvs.cc#L106
            for (auto &[outputName, optOutputPath] :
                 packageInfo.queryOutputs(false)) {
                if (!optOutputPath) {
                    continue;
                }
                outputs[outputName] =
                    localStore->printStorePath(*optOutputPath);
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
        for (const auto &metaName : packageInfo.queryMetaNames()) {
            nix::NixStringContext context;
            std::stringstream ss;

            auto *metaValue = packageInfo.queryMeta(metaName);
            // Skip non-serialisable types
            // TODO: Fix serialisation of derivations to store paths
            if (metaValue == nullptr) {
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
        for (const auto &outputName : inputNode.value) {
            inputDrvOutputs.insert(outputName);
        }
        inputDrvs[localStore->printStorePath(inputDrvPath)] = inputDrvOutputs;
    }
    name = packageInfo.queryName();
    system = drv.platform;
}

void to_json(nlohmann::json &json, const Drv &drv) {
    std::map<std::string, nlohmann::json> outputsJson;
    for (const auto &[name, optPath] : drv.outputs) {
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
