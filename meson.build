project(
  'nix-eval-jobs',
  'cpp',
  version : '0.1.6',
  license : 'GPL-3.0',
)

threads_dep = dependency('threads', required: true)
nlohmann_json_dep = dependency('nlohmann_json', required: true)
libcurl_dep = dependency('libcurl', required: true)

nix_cmd_dep = dependency('nix-cmd', required: true)
nix_expr_dep = dependency('nix-expr', required: true)
nix_fetchers_dep = dependency('nix-fetchers', required: true)
nix_flake_dep = dependency('nix-flake', required: true)
nix_main_dep = dependency('nix-main', required: true)
nix_store_dep = dependency('nix-store', required: true)

subdir('src')
