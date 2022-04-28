#!/usr/bin/env python3

import subprocess
import json
from tempfile import TemporaryDirectory
from pathlib import Path
from typing import List

TEST_ROOT = Path(__file__).parent.resolve()
PROJECT_ROOT = TEST_ROOT.parent
BIN = PROJECT_ROOT.joinpath("build", "src", "nix-eval-jobs")


def common_test(extra_args: List[str]) -> None:
    with TemporaryDirectory() as tempdir:
        cmd = [str(BIN), "--gc-roots-dir", tempdir, "--meta"] + extra_args
        res = subprocess.run(
            cmd,
            cwd=TEST_ROOT.joinpath("assets"),
            text=True,
            check=True,
            stdout=subprocess.PIPE,
        )

        results = [json.loads(r) for r in res.stdout.split("\n") if r]
        assert len(results) == 3

        built_job = results[0]
        assert built_job["path"] == [ "builtJob" ]
        assert built_job["name"] == "job1"
        assert built_job["outputs"]["out"].startswith("/nix/store")
        assert built_job["drvPath"].endswith(".drv")
        assert built_job["meta"]['broken'] is False

        nested_job = results[1]
        assert nested_job["path"] == ["nested.job" ]
        assert nested_job["name"].startswith("hello-")

        substituted_job = results[2]
        assert substituted_job["path"] == [ "substitutedJob" ]
        assert substituted_job["name"].startswith("hello-")
        assert substituted_job["meta"]['broken'] is False

def list_test(extra_args: List[str]) -> None:
    with TemporaryDirectory() as tempdir:
        cmd = [str(BIN), "--gc-roots-dir", tempdir, "--meta"] + extra_args
        res = subprocess.run(
            cmd,
            cwd=TEST_ROOT.joinpath("assets"),
            text=True,
            check=True,
            stdout=subprocess.PIPE,
        )

        results = [json.loads(r) for r in res.stdout.split("\n") if r]
        assert len(results) == 2

        built_job = results[0]
        assert built_job["path"] == [ 0 ]
        assert built_job["name"] == "job1"
        assert built_job["outputs"]["out"].startswith("/nix/store")
        assert built_job["drvPath"].endswith(".drv")
        assert built_job["meta"]['broken'] is False

        substituted_job = results[1]
        assert substituted_job["path"] == [ 1 ]
        assert substituted_job["name"].startswith("hello-")
        assert substituted_job["meta"]['broken'] is False

def single_drv_test(extra_args: List[str]) -> None:
    with TemporaryDirectory() as tempdir:
        cmd = [str(BIN), "--gc-roots-dir", tempdir, "--meta"] + extra_args
        res = subprocess.run(
            cmd,
            cwd=TEST_ROOT.joinpath("assets"),
            text=True,
            check=True,
            stdout=subprocess.PIPE,
        )

        results = [json.loads(r) for r in res.stdout.split("\n") if r]
        assert len(results) == 1

        built_job = results[0]
        assert built_job["name"] == "job1"
        assert built_job["outputs"]["out"].startswith("/nix/store")
        assert built_job["drvPath"].endswith(".drv")
        assert built_job["meta"]['broken'] is False

def test_flake() -> None:
    common_test(["--flake", ".#hydraJobs"])


def test_expression() -> None:
    common_test(["ci.nix"])
    list_test(["list.nix"])
    single_drv_test(["drv.nix"])
