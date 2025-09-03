#pragma once

#include <nix/store/store-api.hh>
#include <nix/store/store-open.hh>
#include <nix/util/ref.hh>
#include <optional>
#include <string>

namespace nix_eval_jobs {

// Helper to open a store from an optional URL
inline auto openStore(std::optional<std::string> storeUrl = std::nullopt)
    -> nix::ref<nix::Store> {
    return storeUrl ? nix::openStore(*storeUrl) : nix::openStore();
}

} // namespace nix_eval_jobs
