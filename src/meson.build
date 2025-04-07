src = [
  'nix-eval-jobs.cc',
  'eval-args.cc',
  'drv.cc',
  'buffered-io.cc',
  'constituents.cc',
  'worker.cc',
  'strings-portable.cc'
]

executable(
  'nix-eval-jobs',
  src,
  dependencies: [
    threads_dep,
    nlohmann_json_dep,
    libcurl_dep,

    nix_main_dep,
    nix_store_dep,
    nix_expr_dep,
    nix_cmd_dep,
    nix_flake_dep,
    nix_fetchers_dep,
  ],
  install: true,
)
