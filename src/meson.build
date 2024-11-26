src = [
  'nix-eval-jobs.cc',
  'eval-args.cc',
  'drv.cc',
  'buffered-io.cc',
  'worker.cc',
  'strings-portable.cc'
]

cc = meson.get_compiler('cpp')

autotool_config = configuration_data()
# nix defines this with autotools
if cc.has_member('struct dirent', 'd_type', prefix: '#include <dirent.h>')
  autotool_config.set('HAVE_STRUCT_DIRENT_D_TYPE', 1)
endif

configure_file(
  input: 'autotools-config.h.in',
  output: 'autotools-config.h',
  configuration: autotool_config
)

executable('nix-eval-jobs', src,
           dependencies : [
             libcurl_dep,
             nix_main_dep,
             nix_store_dep,
             nix_expr_dep,
             nix_cmd_dep,
             nix_flake_dep,
             nix_fetchers_dep,
             boost_dep,
             nlohmann_json_dep,
             threads_dep
           ],
           install: true,
           cpp_args: [
             '-std=c++2a',
             '--include', 'autotools-config.h',
             '--include', 'nix/config-cmd.hh',
             '--include', 'nix/config-expr.hh',
             '--include', 'nix/config-main.hh',
             '--include', 'nix/config-store.hh',
             '--include', 'nix/config-util.hh',
           ])
