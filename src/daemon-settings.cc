// Dummy settings to suppress warnings for daemon-only configuration options.
//
// The nix daemon defines settings like `trusted-users` and `allowed-users` in
// src/nix/unix/daemon.cc, which is not exposed as a library. When nix-eval-jobs
// reads nix.conf, it encounters these settings but cannot recognize them,
// causing "unknown setting" warnings. This file registers dummy settings to
// silence those warnings.

#include <nix/util/config-global.hh>
#include <nix/util/configuration.hh>

namespace {

struct DaemonSettings : nix::Config {
    nix::Setting<nix::Strings> trustedUsers{this,
                                            {"root"},
                                            "trusted-users",
                                            R"(
          A list of user names, separated by whitespace.
          These users will have additional rights when connecting to the
          Nix daemon. This setting is only relevant for the Nix daemon.
        )"};

    nix::Setting<nix::Strings> allowedUsers{this,
                                            {"*"},
                                            "allowed-users",
                                            R"(
          A list of user names, separated by whitespace.
          These users are allowed to connect to the Nix daemon.
          This setting is only relevant for the Nix daemon.
        )"};
};

DaemonSettings daemonSettings;

// NOLINTNEXTLINE(cert-err58-cpp)
nix::GlobalConfig::Register rDaemonSettings(&daemonSettings);

} // namespace
