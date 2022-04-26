#include <nix/eval.hh>
#include <nix/get-drvs.hh>

#include <nlohmann/json.hpp>

using namespace nix;

namespace nix_eval_jobs {

/* The fields of a derivation that are printed in json form */
struct Drv {
    std::string name;
    std::string system;
    std::string drvPath;
    std::map<std::string, std::string> outputs;
    std::optional<nlohmann::json> meta;
    Drv (EvalState & state, DrvInfo & drvInfo, bool meta);
};

static void to_json(nlohmann::json & json, const Drv & drv) {
    json = nlohmann::json{
        { "name", drv.name },
        { "system", drv.system },
        { "drvPath", drv.drvPath },
        { "outputs", drv.outputs },
    };

    if (drv.meta.has_value())
        json["meta"] = drv.meta.value();
}

}
