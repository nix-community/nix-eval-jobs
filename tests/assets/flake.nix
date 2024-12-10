{
  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";

  outputs =
    { self, nixpkgs, ... }:
    let
      pkgs = nixpkgs.legacyPackages.x86_64-linux;
    in
    {
      hydraJobs = import ./ci.nix { inherit pkgs; };

      legacyPackages.x86_64-linux = {
        brokenPkgs = {
          brokenPackage = throw "this is an evaluation error";
        };
        infiniteRecursionPkgs = {
          packageWithInfiniteRecursion =
            let
              recursion = [ recursion ];
            in
            derivation {
              inherit (pkgs) system;
              name = "drvB";
              recursiveAttr = recursion;
              builder = ":";
            };
        };
        success = {
          indirect_aggregate =
            pkgs.runCommand "indirect_aggregate"
              {
                _hydraAggregate = true;
                constituents = [
                  "anotherone"
                ];
              }
              ''
                touch $out
              '';
          direct_aggregate =
            pkgs.runCommand "direct_aggregate"
              {
                _hydraAggregate = true;
                constituents = [
                  self.hydraJobs.builtJob
                ];
              }
              ''
                touch $out
              '';
          mixed_aggregate =
            pkgs.runCommand "mixed_aggregate"
              {
                _hydraAggregate = true;
                constituents = [
                  self.hydraJobs.builtJob
                  "anotherone"
                ];
              }
              ''
                touch $out
              '';
          anotherone = pkgs.writeText "constituent" "text";
        };
        failures = {
          aggregate =
            pkgs.runCommand "aggregate"
              {
                _hydraAggregate = true;
                constituents = [
                  "doesntexist"
                  "doesnteval"
                ];
              }
              ''
                touch $out
              '';
          doesnteval = pkgs.writeText "constituent" (toString { });
        };
      };
    };
}
