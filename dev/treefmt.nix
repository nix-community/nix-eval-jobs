{ pkgs, ... }:
{
  # Used to find the project root
  projectRootFile = "flake.lock";

  programs.deno.enable = true;
  programs.yamlfmt.enable = true;

  programs.clang-format.enable = true;
  programs.clang-format.package = pkgs.llvmPackages_latest.clang-tools;

  programs.deadnix.enable = true;
  programs.nixfmt.enable = true;
  programs.ruff.format = true;
  programs.ruff.check = true;
}
