#include <nix/eval.hh>
#include <nix/get-drvs.hh>
#include <nix/local-fs-store.hh>
#include <nix/value-to-json.hh>

#include <nlohmann/json.hpp>
#include "accessor.hh"

using namespace nix;

namespace nix_eval_jobs {

class Accessor;
struct AccessorPath;

/* JobEvalResult := JobChildren (Vector Accessor) | Drv

   What you get from evaluating a Job. Might be more children to
   evaluate or a leaf Drv.
 */
class JobEvalResult {
public:
    /* toJson : JobEvalResult -> json */
    virtual nlohmann::json toJson() = 0;
    virtual ~JobEvalResult() { };
};

/* Job := Drv | JobAttrs | JobList

   JobAttrs := Attrs Job

   JobList := List Job

   The types of expressions nix-eval-jobs can evaluate

   The implementation (i.e. with JobChildren as children) is a
   different than the grammar because of the way AccessorPath is used
   to walk Jobs.

   Create one with `getJob` or by traversing a Value with
   `AccessorPath::walk`.

   Use it by `eval`ing it.
 */
class Job {
public:
    /* eval : Job -> EvalState -> JobEvalResult */
    virtual std::unique_ptr<JobEvalResult> eval(EvalState & state) = 0;
    virtual ~Job() { };
};

/* a plain drv - the primitive for nix-eval-jobs */
struct Drv : Job, JobEvalResult {
    std::string name;
    std::string system;
    std::string drvPath;
    std::map<std::string, std::string> outputs;
    std::optional<nlohmann::json> meta;
    Drv(EvalState & state, Value & v);
    std::unique_ptr<JobEvalResult> eval(EvalState & state) override;
    nlohmann::json toJson() override;
    ~Drv() { }
};

/* which Jobs are collections */
class HasChildren {
public:
    virtual std::vector<std::unique_ptr<Accessor>> children() = 0;
    virtual ~HasChildren() { };
};

/* The forest Jobs when Job is a collection

   Get one by `eval`ing a Job.
 */
struct JobChildren : JobEvalResult {
    std::vector<std::unique_ptr<Accessor>> children;
    JobChildren(HasChildren & children);
    nlohmann::json toJson() override;
    ~JobChildren() { }
};

/* An attrset of Job */
struct JobAttrs : Job, HasChildren {
    Value * v;
    JobAttrs(EvalState & state, Bindings & autoArgs, Value & vIn);
    std::vector<std::unique_ptr<Accessor>> children() override;
    std::unique_ptr<JobEvalResult> eval(EvalState & state) override;
    ~JobAttrs() { }
};

/* A list of Job */
struct JobList : Job, HasChildren {
    Value * v;
    JobList(EvalState & state, Bindings & autoArgs, Value & vIn);
    std::vector<std::unique_ptr<Accessor>> children() override;
    std::unique_ptr<JobEvalResult> eval(EvalState & state) override;
    ~JobList() { }
};

/* Parse a Job from a nix value */
/* Ignore unused function warnings, as it is actually used  in `main` */
#ifdef __GNUC__
#pragma GCC diagnostic ignored "-Wunused-function"
#elif __clang__
#pragma clang diagnostic ignored "-Wunused-function"
#endif
static std::unique_ptr<Job> getJob(EvalState & state, Bindings & autoArgs, Value & v) {
    try {
        return std::make_unique<Drv>(state, v);
    } catch (TypeError & _) {
        try {
            return std::make_unique<JobAttrs>(state, autoArgs, v);
        } catch (TypeError & _) {
            try {
                return std::make_unique<JobList>(state, autoArgs, v);
            } catch (TypeError & _) {
                throw TypeError("error creating job, expecting one of a derivation, an attrset or a derivation, got: %s", showType(v));
            }
        }
    }
}
#ifdef __GNUC__
#pragma GCC diagnostic warning "-Wunused-function"
#elif __clang__
#pragma clang diagnostic warning "-Wunused-function"
#endif

}
