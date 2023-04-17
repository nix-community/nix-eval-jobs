{ pkgs ? (
    let
      inherit (builtins) fromJSON readFile;
      flakeLock = fromJSON (readFile ./flake.lock);
      locked = flakeLock.nodes.nixpkgs.locked;
      nixpkgs = assert locked.type == "github"; builtins.fetchTarball {
        url = "https://github.com/${locked.owner}/${locked.repo}/archive/${locked.rev}.tar.gz";
        sha256 = locked.narHash;
      };
    in
    import nixpkgs { }
  )
, srcDir ? null
, nix
}:

let
  inherit (pkgs) lib;
  nix-eval-jobs = pkgs.callPackage ./default.nix {
    inherit srcDir nix;
  };
  # nix 2.15 no longer compiles with clang11
  stdenv = if pkgs.stdenv.hostPlatform.isDarwin then pkgs.clang12Stdenv else pkgs.stdenv;
in
stdenv.mkDerivation {
  name = "shell";
  phases = [ "buildPhase" ];

  buildPhase = ''
    touch $out
  '';

  inherit (nix-eval-jobs) buildInputs;
  nativeBuildInputs = nix-eval-jobs.nativeBuildInputs ++ [
    pkgs.treefmt
    # only import clang-format without clang
    (pkgs.runCommand "clang-format" { } ''
      mkdir -p $out/bin
      ln -s ${pkgs.llvmPackages.clang-unwrapped}/bin/clang-format $out/bin/clang-format
    '')
    pkgs.nixpkgs-fmt
    pkgs.nodePackages.prettier

    (pkgs.python3.withPackages (ps: [
      ps.pytest
      ps.black
    ]))

  ];
  NODE_PATH = "${pkgs.nodePackages.prettier-plugin-toml}/lib/node_modules";

  shellHook = lib.optionalString stdenv.isLinux ''
    export NIX_DEBUG_INFO_DIRS="${pkgs.curl.debug}/lib/debug:${nix.debug}/lib/debug''${NIX_DEBUG_INFO_DIRS:+:$NIX_DEBUG_INFO_DIRS}"
  '';
}
