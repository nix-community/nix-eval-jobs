#include <nix/store/path-with-outputs.hh>
#include <nix/store/store-api.hh>
#include <nix/store/local-fs-store.hh>
#include <nix/store/globals.hh>
#include <nix/expr/value-to-json.hh>
#include <nix/store/derivations.hh>
#include <nix/store/derivation-options.hh>
#include <nix/expr/get-drvs.hh>
#include <nix/store/derived-path-map.hh>
#include <nix/expr/eval.hh>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>
#include <nix/store/path.hh>
#include <nix/util/ref.hh>
#include <nix/expr/value/context.hh>
#include <nix/util/error.hh>
#include <nix/expr/eval-error.hh>
#include <nix/util/experimental-features.hh>
#include <nix/util/configuration.hh> // for experimentalFeatureSettings
// required for std::optional
#include <nix/util/json-utils.hh> //NOLINT(misc-include-cleaner)
#include <nix/util/pos-idx.hh>
#include <exception>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
#include <algorithm>

#include "drv.hh"
#include "eval-args.hh"

namespace {

auto queryOutputs(nix::PackageInfo &packageInfo, nix::EvalState &state,
                  const std::string &attrPath)
    -> std::map<std::string, std::optional<std::string>> {
    std::map<std::string, std::optional<std::string>> outputs;

    try {
        nix::PackageInfo::Outputs outputsQueried;

        // CA derivations do not have static output paths, so we have to
        // fallback if we encounter an error
        try {
            outputsQueried = packageInfo.queryOutputs(true);
        } catch (const nix::Error &e) {
            // Handle CA derivation errors
            if (!nix::experimentalFeatureSettings.isEnabled(
                    nix::Xp::CaDerivations)) {
                throw;
            }
            outputsQueried = packageInfo.queryOutputs(false);
        }
        for (auto &[outputName, optOutputPath] : outputsQueried) {
            if (optOutputPath) {
                outputs[outputName] =
                    state.store->printStorePath(*optOutputPath);
            } else {
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

    return outputs;
}

auto queryMeta(nix::PackageInfo &packageInfo, nix::EvalState &state)
    -> std::optional<nlohmann::json> {
    nlohmann::json meta_;
    for (const auto &metaName : packageInfo.queryMetaNames()) {
        nix::NixStringContext context;
        std::stringstream stream;

        auto *metaValue = packageInfo.queryMeta(metaName);
        // Skip non-serialisable types
        if (metaValue == nullptr) {
            continue;
        }

        nix::printValueAsJSON(state, true, *metaValue, nix::noPos, stream,
                              context);

        meta_[metaName] = nlohmann::json::parse(stream.str());
    }
    return meta_;
}

auto queryInputDrvs(const nix::Derivation &drv, nix::Store &store)
    -> std::map<std::string, std::set<std::string>> {
    std::map<std::string, std::set<std::string>> drvs;
    for (const auto &[inputDrvPath, inputNode] : drv.inputDrvs.map) {
        std::set<std::string> inputDrvOutputs;
        for (const auto &outputName : inputNode.value) {
            inputDrvOutputs.insert(outputName);
        }
        drvs[store.printStorePath(inputDrvPath)] = inputDrvOutputs;
    }
    return drvs;
}

auto queryCacheStatus(
    nix::Store &store,
    std::map<std::string, std::optional<std::string>> &outputs,
    std::vector<std::string> &neededBuilds,
    std::vector<std::string> &neededSubstitutes,
    std::vector<std::string> &unknownPaths, const nix::Derivation &drv)
    -> Drv::CacheStatus {

    std::vector<nix::StorePathWithOutputs> paths;
    // Add output paths
    for (auto const &[key, val] : outputs) {
        if (val) {
            paths.push_back(followLinksToStorePathWithOutputs(store, *val));
        }
    }

    // Add input derivation paths
    for (const auto &[inputDrvPath, inputNode] : drv.inputDrvs.map) {
        paths.push_back(
            nix::StorePathWithOutputs(inputDrvPath, inputNode.value));
    }

    auto missing = store.queryMissing(toDerivedPaths(paths));

    if (!missing.willBuild.empty()) {
        // TODO: can we expose the topological sort order as a graph?
        auto sorted = store.topoSortPaths(missing.willBuild);
        std::ranges::reverse(sorted.begin(), sorted.end());
        for (auto &path : sorted) {
            neededBuilds.push_back(store.printStorePath(path));
        }
    }
    if (!missing.willSubstitute.empty()) {
        std::vector<const nix::StorePath *> willSubstituteSorted = {};
        std::ranges::for_each(missing.willSubstitute.begin(),
                              missing.willSubstitute.end(),
                              [&](const nix::StorePath &path) -> void {
                                  willSubstituteSorted.push_back(&path);
                              });
        std::ranges::sort(
            willSubstituteSorted.begin(), willSubstituteSorted.end(),
            [](const nix::StorePath *lhs, const nix::StorePath *rhs) -> bool {
                if (lhs->name() == rhs->name()) {
                    return lhs->to_string() < rhs->to_string();
                }
                return lhs->name() < rhs->name();
            });
        for (const auto *path : willSubstituteSorted) {
            neededSubstitutes.push_back(store.printStorePath(*path));
        }
    }

    if (!missing.unknown.empty()) {
        for (const auto &path : missing.unknown) {
            unknownPaths.push_back(store.printStorePath(path));
        }
    }

    if (missing.willBuild.empty() && missing.unknown.empty()) {
        if (missing.willSubstitute.empty()) {
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
};

} // namespace

/* The fields of a derivation that are printed in json form */
Drv::Drv(std::string &attrPath, nix::EvalState &state,
         nix::PackageInfo &packageInfo, MyArgs &args,
         std::optional<Constituents> constituents)
    : constituents(std::move(constituents)) {

    auto store = state.store;

    name = packageInfo.queryName();

    // Query outputs using helper function
    outputs = queryOutputs(packageInfo, state, attrPath);
    drvPath = store->printStorePath(packageInfo.requireDrvPath());

    // Check if we can read derivations (requires LocalFSStore and not in
    // read-only mode)
    auto localStore = store.dynamic_pointer_cast<nix::LocalFSStore>();
    const bool canReadDerivation = localStore && !nix::settings.readOnlyMode;

    if (canReadDerivation) {
        // We can read the derivation directly for precise information
        auto drv = localStore->readDerivation(packageInfo.requireDrvPath());

        // Use the more precise system from the derivation
        system = drv.platform;

        if (args.checkCacheStatus) {
            // TODO: is this a bottleneck, where we should batch these queries?
            cacheStatus =
                queryCacheStatus(*store, outputs, neededBuilds,
                                 neededSubstitutes, unknownPaths, drv);
        } else {
            cacheStatus = Drv::CacheStatus::Unknown;
        }

        if (args.showInputDrvs) {
            inputDrvs = queryInputDrvs(drv, *store);
        }

        auto drvOptions = nix::derivationOptionsFromStructuredAttrs(
            *store, drv.env,
            drv.structuredAttrs ? &*drv.structuredAttrs : nullptr);
        requiredSystemFeatures =
            std::optional(drvOptions.getRequiredSystemFeatures(drv));
    } else {
        // Fall back to basic info from PackageInfo
        // This happens when:
        // - In read-only/no-instantiate mode
        // - Store is not a LocalFSStore (e.g., remote store)
        system = packageInfo.querySystem();
        cacheStatus = Drv::CacheStatus::Unknown;
        // Can't get input derivations without reading the .drv file
    }

    // Handle metadata (works in both modes)
    if (args.meta) {
        meta = queryMeta(packageInfo, state);
    }
}

void to_json(nlohmann::json &json, const Drv &drv) {
    json = nlohmann::json{{"name", drv.name},
                          {"system", drv.system},
                          {"drvPath", drv.drvPath},
                          {"outputs", drv.outputs}};

    if (drv.meta.has_value()) {
        json["meta"] = drv.meta.value();
    }
    if (drv.inputDrvs) {
        json["inputDrvs"] = drv.inputDrvs.value();
    }

    if (drv.requiredSystemFeatures) {
        json["requiredSystemFeatures"] = drv.requiredSystemFeatures.value();
    }

    if (auto constituents = drv.constituents) {
        json["constituents"] = constituents->constituents;
        json["namedConstituents"] = constituents->namedConstituents;
        json["globConstituents"] = constituents->globConstituents;
    }

    if (drv.cacheStatus != Drv::CacheStatus::Unknown) {
        // Deprecated field
        json["isCached"] = drv.cacheStatus == Drv::CacheStatus::Cached ||
                           drv.cacheStatus == Drv::CacheStatus::Local;

        switch (drv.cacheStatus) {
        case Drv::CacheStatus::Cached:
            json["cacheStatus"] = "cached";
            break;
        case Drv::CacheStatus::Local:
            json["cacheStatus"] = "local";
            break;
        default:
            json["cacheStatus"] = "notBuilt";
            break;
        }
        json["neededBuilds"] = drv.neededBuilds;
        json["neededSubstitutes"] = drv.neededSubstitutes;
        // TODO: is it useful to include "unknown" paths at all?
        // json["unknown"] = drv.unknownPaths;
    }
}
