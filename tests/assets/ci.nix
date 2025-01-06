{
  pkgs ? import (builtins.getFlake (toString ./.)).inputs.nixpkgs { },
  system ? pkgs.system,
}:

let
  dep-a = pkgs.runCommand "dep-a" { } ''
    mkdir -p $out
    echo "bbbbbb" > $out/dep-b
  '';

  dep-b = pkgs.runCommand "dep-b" { } ''
    mkdir -p $out
    echo "aaaaaa" > $out/dep-b
  '';
in
{
  builtJob = pkgs.writeText "job1" "job1";
  substitutedJob = pkgs.nix;

  dontRecurse = {
    # This shouldn't build as `recurseForDerivations = true;` is not set
    # recurseForDerivations = true;

    # This should not build
    drvB = derivation {
      inherit system;
      name = "drvA";
      builder = ":";
    };
  };

  "dotted.attr" = pkgs.nix;

  package-with-deps = pkgs.runCommand "package-with-deps" { } ''
    mkdir -p $out
    cp -r ${dep-a} $out/dep-a
    cp -r ${dep-b} $out/dep-b
  '';

  recurse = {
    # This should build
    recurseForDerivations = true;

    # This should not build
    drvB = derivation {
      inherit system;
      name = "drvB";
      builder = ":";
    };
  };
}
