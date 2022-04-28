#include <nix/config.h>
#include <nix/eval.hh>
#include <nlohmann/json.hpp>

#include "args.hh"
#include "job.hh"
#include "accessor.hh"

using namespace nix;

namespace nix_eval_jobs {

/* Parse an accessor from json, the introduction rule. */
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

/* Accessor */

Index::Index(const nlohmann::json & json) {
    try {
        val = json;
    } catch (...)  {
        throw TypeError("could not make an index out of json: %s", json.dump());
    }
}

Index::Index(unsigned long val) {
    this->val = val;
}

Index::Index(const Index & that) {
    this->val = that.val;
}

Name::Name(const nlohmann::json & json) {
    try {
        val = json;
        if (val.empty()) throw EvalError("empty attribute name");
    } catch (...) {
        throw TypeError("could not create an attrname out of json: %s", json.dump());
    }
}

Name::Name(Symbol & sym) {
    std::stringstream ss;
    ss << sym;
    this->val = ss.str();
}

Name::Name(const Name & that) {
    this->val = that.val;
}

/* getIn : Accessor -> EvalState -> Bindings -> Value -> Value */

Value * Index::getIn(EvalState & state, Bindings & autoArgs, Value & v) {

    if (v.type() != nList)
        throw TypeError("tried to get an index in %s", showType(v));

    if (val >= v.listSize())
        throw EvalError("index %d out of bounds", val);

    return v.listElems()[val];
}

Value * Name::getIn(EvalState & state, Bindings & autoArgs, Value & v) {
    if (v.type() != nAttrs)
        throw TypeError("tried to get an attrname in %s", showType(v));

    auto pair = v.attrs->find(state.symbols.create(val));

    if (pair) return pair->value;
    else throw EvalError("name not in attrs: '%s'", val);
}

/* toJson : Accessor -> json */

nlohmann::json Name::toJson() {
    return val;
}

nlohmann::json Index::toJson() {
    return val;
}

/* AccessorPath */

AccessorPath::AccessorPath(std::string & s) {
    nlohmann::json json;
    try {
        json = nlohmann::json::parse(s);

    } catch (nlohmann::json::exception & e) {
        throw TypeError("error parsing accessor path json: %s", s);
    }

    try {
        std::vector<nlohmann::json> vec = json;
        for (auto j : vec)
            this->path.push_back(accessorFromJson(j));

    } catch (nlohmann::json::exception & e) {
        throw TypeError("could not make an accessor path out of json, expected a list of accessors: %s", json.dump());
    }
}

/* walk : AccessorPath -> EvalState -> Bindings -> Value -> Job */

std::unique_ptr<Job> AccessorPath::walk(MyArgs & myArgs, EvalState & state, Bindings & autoArgs, Value & vRoot) {
    Value * v = &vRoot;

    for (auto & a : path)
        v = a->getIn(state, autoArgs, *v);

    auto vRes = state.allocValue();
    state.autoCallFunction(autoArgs, *v, *vRes);

    return getJob(myArgs, state, autoArgs, *vRes);
}

/* toJson : ToJson -> json */

nlohmann::json AccessorPath::toJson() {
    std::vector<nlohmann::json> res;
    for (auto & a : path)
        res.push_back(a->toJson());

    return res;
}

}
