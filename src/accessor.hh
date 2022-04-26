#include <nix/eval.hh>
#include <nlohmann/json.hpp>

using namespace nix;

namespace nix_eval_jobs {

/* Ways to look into a value */
class Accessor {
public:
    virtual Value * getIn(EvalState & state, Bindings & autoArgs, Value & v) = 0;

    virtual nlohmann::json toJson() = 0;

    virtual ~Accessor() {}
};

/* An index into a list */
struct Index : Accessor {
    unsigned long val;

    Index(const nlohmann::json & json) {
        try {
            val = json;
        } catch (...)  {
            throw TypeError("could not make an index out of json: %s", json.dump());
        }
    }

    Value * getIn(EvalState & state, Bindings & autoArgs, Value & v) {

        if (v.type() != nList)
            throw TypeError("tried to get an index in %s", showType(v));

        if (val >= v.listSize())
            throw EvalError("index %d out of bounds", val);

        return v.listElems()[val];
    }

    nlohmann::json toJson() {
        return val;
    }
};

/* An attribute name in an attrset */
struct Name : Accessor {
    std::string val;

    Name(const nlohmann::json & json) {
        try {
            val = json;
            if (val.empty()) throw EvalError("empty attribute name");
        } catch (...) {
            throw TypeError("could not create an attrname out of json: %s", json.dump());
        }
    }

    Value * getIn(EvalState & state, Bindings & autoArgs, Value & v) {
        if (v.type() != nAttrs)
            throw TypeError("tried to get an attrname in %s", showType(v));

        auto pair = v.attrs->find(state.symbols.create(val));

        if (pair) return pair->value;
        else throw EvalError("name not in attrs: '%s'", val);
    }

    nlohmann::json toJson() {
        return val;
    }
};

static std::unique_ptr<Accessor> accessorFromJson(const nlohmann::json & json) {
    try {
        return std::make_unique<Index>(json);
    } catch (...) {
        try {
            return std::make_unique<Name>(json);
        } catch (...) {
            throw TypeError("could not make an accessor out of json: %s", json.dump());
        }
    }
}

struct AccessorPath {
    std::vector<std::unique_ptr<Accessor>> path;

    AccessorPath(std::string & s) {
        nlohmann::json json;
        try {
            json = nlohmann::json::parse(s);

        } catch (nlohmann::json::exception & e) {
            throw TypeError("error parsing accessor path json: %s", s);
        }

        try {
            std::vector<nlohmann::json> vec;
            for (auto j : vec)
                this->path.push_back(std::move(accessorFromJson(j)));

        } catch (nlohmann::json::exception & e) {
            throw TypeError("could not make an accessor path out of json, expected a list of accessors: %s", json.dump());
        }
    }

    std::optional<Value *> walk(EvalState & state, Bindings & autoArgs, Value & vTop) {
        Value * v = &vTop;

        if (path.empty())
            return std::nullopt;

        for (auto & a : path)
            v = a->getIn(state, autoArgs, *v);

        return v;
    }

    nlohmann::json toJson() {
        std::vector<nlohmann::json> res;
        for (auto & a : path)
            res.push_back(a->toJson());

        return res;
    }

    ~AccessorPath() { }
};

}
