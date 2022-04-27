#include <nix/eval.hh>
#include <nix/get-drvs.hh>
#include <nix/local-fs-store.hh>
#include <nix/value-to-json.hh>

#include <nlohmann/json.hpp>

#include "args.hh"

using namespace nix;

namespace nix_eval_jobs {

class Accessor;

/* The "forest" part of the job ast when Job is a collection */
typedef std::vector<std::unique_ptr<Accessor>> JobChildren;

/* The types of expressions nix-eval-jobs can evaluate

   Job := JobDrv | JobAttrs | JobList

   JobAttrs := Attrs<Job>

   JobList := List<Job>

   The implementation (i.e. with JobChildren as children) is a
   different than the grammar because of the way AccessorPath is used
   to walk Jobs.

   Create one with `getJob`
 */
class Job {
public:
    virtual std::optional<JobChildren> children() = 0;
    virtual ~Job() = default;
};

/* A plain drv - the primitive for nix-eval-jobs */
struct JobDrv : Job {
    DrvInfo * drvInfo;
    JobDrv(EvalState & state, Value & v);
    JobDrv(DrvInfo * d);
    ~JobDrv();
};

/* An attrset of Job */
struct JobAttrs : Job {
    Value * v;
    JobAttrs(EvalState & state, Bindings & autoArgs, Value & vIn);
    ~JobAttrs() { }
};

/* A list of Job */
struct JobList : Job {
    Value * v;
    JobList(EvalState & state, Bindings & autoArgs, Value & vIn);
    ~JobList() { }
};

/* Parse a Job from a nix value */
std::unique_ptr<Job> getJob(EvalState & state, Bindings & autoArgs, Value & v);

}
