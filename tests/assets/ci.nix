{
  system ? "x86_64-linux",
}:

let
  dep-a = derivation {
    name = "dep-a";
    inherit system;
    builder = "/bin/sh";
    args = [
      "-c"
      "echo 'bbbbbb' > $out"
    ];
  };

  dep-b = derivation {
    name = "dep-b";
    inherit system;
    builder = "/bin/sh";
    args = [
      "-c"
      "echo 'aaaaaa' > $out"
    ];
  };
in
{
  builtJob = derivation {
    name = "job1";
    inherit system;
    builder = "/bin/sh";
    args = [
      "-c"
      "echo 'job1' > $out"
    ];
    requiredSystemFeatures = [ "big-parallel" ];
  };

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

  "dotted.attr" = derivation {
    name = "dotted";
    inherit system;
    builder = "/bin/sh";
    args = [
      "-c"
      "echo 'dotted' > $out"
    ];
  };

  package-with-deps = derivation {
    name = "package-with-deps";
    inherit system;
    builder = "/bin/sh";
    args = [
      "-c"
      "echo '${dep-a} ${dep-b}' > $out"
    ];
  };

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
