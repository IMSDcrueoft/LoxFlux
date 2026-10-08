#!/usr/bin/env python3
"""LoxFlux test suite runner.

Usage:
    python run_tests.py [--exe <path to loxFlux.exe>] [--timeout N]

Test files are *.lox files under cases/ and generated/ (next to this
script). Expectations are declared in the leading `//` comment header:

    // exit: 0          exact process exit code (default 0)
    // out: <line>      expected stdout line, matched in order; the number of
    //                  `out:` lines must equal the number of stdout lines
    // err: <substring> substring that must appear in stderr (at least once)
    // err!: <substring> substring that must NOT appear in stderr

The interpreter is launched with the test file's directory as the working
directory, so `import "./lib.lox"` resolves relative to the test file.
"""

import argparse
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent


def find_exe() -> Path | None:
    candidates = [
        HERE.parent.parent / "x64" / "Release" / "loxFlux.exe",
        HERE.parent.parent / "x64" / "Debug" / "loxFlux.exe",
    ]
    for c in candidates:
        if c.exists():
            return c
    return None


def parse_expectations(file: Path):
    exp = {"exit": 0, "out": [], "err": [], "err!": []}
    for line in file.read_text(encoding="utf-8", errors="replace").splitlines()[:60]:
        if not line.startswith("//"):
            break
        body = line[2:].strip()
        if body.startswith("exit:"):
            exp["exit"] = int(body[5:].strip())
        elif body.startswith("out:"):
            exp["out"].append(body[4:].strip())
        elif body.startswith("err!:"):
            exp["err!"].append(body[5:].strip())
        elif body.startswith("err:"):
            exp["err"].append(body[4:].strip())
    return exp


def is_test_file(file: Path) -> bool:
    """Only files that declare expectations are runnable cases; the rest
    (import helper modules without a `// exit:` header) are skipped."""
    text = file.read_text(encoding="utf-8", errors="replace").splitlines()[:60]
    for line in text:
        if not line.startswith("//"):
            break
        if line[2:].strip().startswith("exit:"):
            return True
    return False


def collect_tests():
    files: list[Path] = []
    for sub in ("cases", "generated"):
        d = HERE / sub
        if d.is_dir():
            files.extend(sorted(f for f in d.rglob("*.lox") if is_test_file(f)))
    return files


def run_case(exe: Path, file: Path, timeout: int):
    proc = subprocess.run(
        [str(exe), str(file)],
        cwd=str(file.parent),
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=timeout,
    )
    return proc.returncode, proc.stdout, proc.stderr


def check(file: Path, exp, code, out, err):
    problems = []
    if code != exp["exit"]:
        problems.append(f"exit={code}, expected {exp['exit']}")

    out_lines = out.splitlines()
    if exp["out"]:
        if len(out_lines) != len(exp["out"]):
            problems.append(
                f"stdout line count {len(out_lines)}, expected {len(exp['out'])}"
            )
        else:
            for i, (got, want) in enumerate(zip(out_lines, exp["out"])):
                if got != want:
                    problems.append(f"stdout[{i}]={got!r}, expected {want!r}")

    for e in exp["err"]:
        if e not in err:
            problems.append(f"stderr missing {e!r}")
    for e in exp["err!"]:
        if e in err:
            problems.append(f"stderr must not contain {e!r}")
    return problems


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", default="")
    ap.add_argument("--timeout", type=int, default=180)
    args = ap.parse_args()

    exe = Path(args.exe) if args.exe else find_exe()
    if exe is None or not exe.exists():
        print("loxFlux.exe not found; pass --exe <path>", file=sys.stderr)
        return 1
    print(f"Using interpreter: {exe}")

    tests = collect_tests()
    if not tests:
        print("No test files found.", file=sys.stderr)
        return 1

    passed, failed = 0, 0
    for file in tests:
        name = file.relative_to(HERE).as_posix()
        exp = parse_expectations(file)
        try:
            code, out, err = run_case(exe, file, args.timeout)
        except subprocess.TimeoutExpired:
            print(f"FAIL  {name}")
            print("      timed out")
            failed += 1
            continue
        problems = check(file, exp, code, out, err)
        if not problems:
            passed += 1
            print(f"PASS  {name}")
        else:
            failed += 1
            print(f"FAIL  {name}")
            for p in problems:
                print(f"      {p}")

    print()
    print(f"Total: {passed + failed}  Pass: {passed}  Fail: {failed}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
