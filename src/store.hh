#pragma once

#include <nix/store/local-fs-store.hh>
#include <nix/store/store-api.hh>
#include <nix/store/store-open.hh>
#include <nix/util/ref.hh>
#include <nix/util/error.hh>
#include <optional>
#include <string>

namespace nix_eval_jobs {

// Helper to open a store from an optional URL
inline nix::ref<nix::Store>
openStore(std::optional<std::string> storeUrl = std::nullopt) {
    return storeUrl ? nix::openStore(*storeUrl) : nix::openStore();
}

} // namespace nix_eval_jobs
