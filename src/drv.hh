#include <nix/get-drvs.hh>
#include <nix/eval.hh>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <map>
#include <set>
#include <string>
#include <optional>

#include "eval-args.hh"

class MyArgs;

namespace nix {
class EvalState;
struct PackageInfo;
} // namespace nix

/* The fields of a derivation that are printed in json form */
struct Drv {
    std::string name;
    std::string system;
    std::string drvPath;

    std::map<std::string, std::optional<std::string>> outputs;

    // TODO: make this optional or remove?
    std::map<std::string, std::set<std::string>> inputDrvs;

    // TODO: can we lazily allocate these?
    std::vector<std::string> neededBuilds;
    std::vector<std::string> neededSubstitutes;
    std::vector<std::string> unknownPaths;

    // TODO: we might not need to store this as it can be computed from the
    // above
    enum class CacheStatus { Local, Cached, NotBuilt, Unknown } cacheStatus;

    std::optional<nlohmann::json> meta;

    Drv(std::string &attrPath, nix::EvalState &state,
        nix::PackageInfo &packageInfo, MyArgs &args);
};
void to_json(nlohmann::json &json, const Drv &drv);
