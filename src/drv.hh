#include <nix/get-drvs.hh>
#include <nix/eval.hh>
#include <nlohmann/json_fwd.hpp>
// we need this include or otherwise we cannot instantiate std::optional
#include <nlohmann/json.hpp> //NOLINT(misc-include-cleaner)
#include <cstdint>
#include <string>
#include <map>
#include <set>
#include <optional>

#include "eval-args.hh"

namespace nix {
class EvalState;
struct PackageInfo;
} // namespace nix

struct Constituents {
    std::vector<std::string> constituents;
    std::vector<std::string> namedConstituents;
    Constituents(std::vector<std::string> constituents,
                 std::vector<std::string> namedConstituents)
        : constituents(std::move(constituents)),
          namedConstituents(std::move(namedConstituents)) {};
};

/* The fields of a derivation that are printed in json form */
struct Drv {
    Drv(std::string &attrPath, nix::EvalState &state,
        nix::PackageInfo &packageInfo, MyArgs &args,
        std::optional<Constituents> constituents);
    std::string name;
    std::string system;
    std::string drvPath;

    enum class CacheStatus : uint8_t {
        Local,
        Cached,
        NotBuilt,
        Unknown
    } cacheStatus;
    std::map<std::string, std::optional<std::string>> outputs;
    std::map<std::string, std::set<std::string>> inputDrvs;
    std::optional<nlohmann::json> meta;
    std::optional<Constituents> constituents;
};
void to_json(nlohmann::json &json, const Drv &drv);
