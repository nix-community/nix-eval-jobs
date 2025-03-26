#include <fnmatch.h>
#include <nix/store-api.hh>
#include <nlohmann/json.hpp>
#include <nix/derivations.hh>
#include <nix/local-fs-store.hh>
#include <nix/topo-sort.hh>

#include "constituents.hh"

namespace {

std::vector<AggregateJob>
topoSort(const std::set<AggregateJob> &aggregateJobs) {
    return nix::topoSort<AggregateJob>(
        aggregateJobs,
        [&aggregateJobs](const AggregateJob &job) {
            std::set<AggregateJob> dependencies;
            for (const auto &depName : job.dependencies) {
                auto it =
                    std::find_if(aggregateJobs.begin(), aggregateJobs.end(),
                                 [&depName](const AggregateJob &j) {
                                     return j.name == depName;
                                 });
                if (it != aggregateJobs.end()) {
                    dependencies.insert(*it);
                }
            }
            return dependencies;
        },
        [&aggregateJobs](const AggregateJob &job, const AggregateJob &parent) {
            std::set<std::string> remaining;
            for (const auto &j : aggregateJobs) {
                remaining.insert(j.name);
            }
            return DependencyCycle(job.name, parent.name, remaining);
        });
}

auto insertMatchingConstituents(
    const std::string &childJobName, const std::string &jobName,
    const std::function<bool(const std::string &, const nlohmann::json &)>
        &isBroken,
    const std::map<std::string, nlohmann::json> &jobs,
    std::set<std::string> &results) -> bool {
    bool expansionFound = false;
    for (const auto &[currentJobName, job] : jobs) {
        // Never select the job itself as constituent. Trivial way
        // to avoid obvious cycles.
        if (currentJobName == jobName) {
            continue;
        }
        auto jobName = currentJobName;
        if (fnmatch(childJobName.c_str(), jobName.c_str(), 0) == 0 &&
            !isBroken(jobName, job)) {
            results.insert(jobName);
            expansionFound = true;
        }
    }

    return expansionFound;
}
} // namespace

auto resolveNamedConstituents(const std::map<std::string, nlohmann::json> &jobs)
    -> std::variant<std::vector<AggregateJob>, DependencyCycle> {
    std::set<AggregateJob> aggregateJobs;
    for (auto const &[jobName, job] : jobs) {
        auto named = job.find("namedConstituents");
        if (named != job.end() && !named->empty()) {
            bool globConstituents = job.value<bool>("globConstituents", false);
            std::unordered_map<std::string, std::string> brokenJobs;
            std::set<std::string> results;

            auto isBroken = [&brokenJobs,
                             &jobName](const std::string &childJobName,
                                       const nlohmann::json &job) -> bool {
                if (job.find("error") != job.end()) {
                    std::string error = job["error"];
                    nix::logger->log(
                        nix::lvlError,
                        nix::fmt(
                            "aggregate job '%s' references broken job '%s': %s",
                            jobName, childJobName, error));
                    brokenJobs[childJobName] = error;
                    return true;
                }
                return false;
            };

            for (const std::string childJobName : *named) {
                auto childJobIter = jobs.find(childJobName);
                if (childJobIter == jobs.end()) {
                    if (!globConstituents) {
                        nix::logger->log(
                            nix::lvlError,
                            nix::fmt("aggregate job '%s' references "
                                     "non-existent job '%s'",
                                     jobName, childJobName));
                        brokenJobs[childJobName] = "does not exist";
                    } else if (!insertMatchingConstituents(childJobName,
                                                           jobName, isBroken,
                                                           jobs, results)) {
                        nix::warn("aggregate job '%s' references constituent "
                                  "glob pattern '%s' with no matches",
                                  jobName, childJobName);
                        brokenJobs[childJobName] =
                            "constituent glob pattern had no matches";
                    }
                } else if (!isBroken(childJobName, childJobIter->second)) {
                    results.insert(childJobName);
                }
            }

            aggregateJobs.insert(AggregateJob(jobName, results, brokenJobs));
        }
    }

    try {
        return topoSort(aggregateJobs);
    } catch (DependencyCycle &e) {
        return e;
    }
}

void rewriteAggregates(std::map<std::string, nlohmann::json> &jobs,
                       const std::vector<AggregateJob> &aggregateJobs,
                       nix::ref<nix::Store> &store, nix::Path &gcRootsDir) {
    for (const auto &aggregateJob : aggregateJobs) {
        auto &job = jobs.find(aggregateJob.name)->second;
        auto drvPath = store->parseStorePath(std::string(job["drvPath"]));
        auto drv = store->readDerivation(drvPath);

        if (aggregateJob.brokenJobs.empty()) {
            for (const auto &childJobName : aggregateJob.dependencies) {
                auto childDrvPath = store->parseStorePath(
                    std::string(jobs.find(childJobName)->second["drvPath"]));
                auto childDrv = store->readDerivation(childDrvPath);
                job["constituents"].push_back(
                    store->printStorePath(childDrvPath));
                drv.inputDrvs.map[childDrvPath].value = {
                    childDrv.outputs.begin()->first};
            }

            std::string drvName(drvPath.name());
            assert(nix::hasSuffix(drvName, nix::drvExtension));
            drvName.resize(drvName.size() - nix::drvExtension.size());

            auto hashModulo = hashDerivationModulo(*store, drv, true);
            if (hashModulo.kind != nix::DrvHash::Kind::Regular) {
                continue;
            }
            auto h = hashModulo.hashes.find("out");
            if (h == hashModulo.hashes.end()) {
                continue;
            }
            auto outPath = store->makeOutputPath("out", h->second, drvName);
            drv.env["out"] = store->printStorePath(outPath);
            drv.outputs.insert_or_assign(
                "out", nix::DerivationOutput::InputAddressed{.path = outPath});

            auto newDrvPath = nix::writeDerivation(*store, drv);
            auto newDrvPathS = store->printStorePath(newDrvPath);

            /* Register the derivation as a GC root.  !!! This
                registers roots for jobs that we may have already
                done. */
            auto localStore = store.dynamic_pointer_cast<nix::LocalFSStore>();
            if (!gcRootsDir.empty()) {
                const nix::Path root =
                    gcRootsDir + "/" +
                    std::string(nix::baseNameOf(newDrvPathS));

                if (!nix::pathExists(root)) {
                    auto localStore =
                        store.dynamic_pointer_cast<nix::LocalFSStore>();
                    localStore->addPermRoot(newDrvPath, root);
                }
            }

            nix::logger->log(nix::lvlDebug,
                             nix::fmt("rewrote aggregate derivation %s -> %s",
                                      store->printStorePath(drvPath),
                                      newDrvPathS));

            job["drvPath"] = newDrvPathS;
            job["outputs"]["out"] = store->printStorePath(outPath);
        }

        job.erase("namedConstituents");

        if (!aggregateJob.brokenJobs.empty()) {
            std::stringstream ss;
            for (const auto &[jobName, error] : aggregateJob.brokenJobs) {
                ss << jobName << ": " << error << "\n";
            }
            job["error"] = ss.str();
        }

        std::cout << job.dump() << "\n" << std::flush;
    }
}
