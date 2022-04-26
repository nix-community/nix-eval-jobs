#include <nix/eval.hh>
#include <nlohmann/json.hpp>

using namespace nix;

namespace nix_eval_jobs {

class Job;

/* Ways to look into a value.  This is how nix-eval-jobs "recurses"
   over nix exprs. Accessor gets the next elem, AccessorPath finds a
   value in nested exprs.

   Accessor := Index | Name
 */
class Accessor {
public:
    virtual Value * getIn(EvalState & state, Bindings & autoArgs, Value & v) = 0;
    virtual nlohmann::json toJson() = 0;
    virtual ~Accessor() {}
};

/* An index into a list */
struct Index : Accessor {
    unsigned long val;
    Index(const nlohmann::json & json);
    Index(unsigned long val);
};

/* An attribute name in an attrset */
struct Name : Accessor {
    std::string val;
    Name(const nlohmann::json & json);
    Name(Symbol & sym);
};

/* Follow a path into a nested nixexpr */
struct AccessorPath {
    std::vector<std::unique_ptr<Accessor>> path;
    AccessorPath(std::string & s);
    std::optional<Job *> walk(EvalState & state, Bindings & autoArgs, Value & vRoot);
    nlohmann::json toJson();
    ~AccessorPath() { }
};

}
