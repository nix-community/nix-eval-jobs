{ pkgs, ... }:
{
  # Used to find the project root
  projectRootFile = "flake.lock";

  programs.deno.enable = true;

  programs.clang-format.enable = true;

  programs.deadnix.enable = true;
  programs.nixfmt.enable = true;
  programs.nixfmt.package = pkgs.nixfmt-rfc-style;
  programs.ruff.format = true;
  programs.ruff.check = true;
}
