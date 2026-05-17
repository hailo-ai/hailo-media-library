#!/usr/bin/env python3
"""Pre-commit hook that runs Include What You Use on changed C++ files.

Requires:
  - include-what-you-use 0.19 installed (see install.sh install_iwyu)
  - compile_commands.json somewhere under the repository root
    (generated automatically by the build, or by merge_compile_commands_util.py)

If compile_commands.json is not found, the hook exits successfully with a
warning — you need at least one build before IWYU can check anything.

Exit codes:
  0  — all files pass (or nothing to check)
  1  — IWYU found issues in one or more files
"""

import json
import os
import re
import shlex
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path
from typing import Dict, List, Optional, Tuple

# IWYU flags that are always passed.
# --no_default_mappings: IWYU 0.19+ has built-in mappings that conflict with
# the external iwyu.gcc.imp referenced from our .iwyu.imp. Disable defaults
# and load everything through our mapping file instead.
IWYU_FLAGS = [
    "-Xiwyu", "--no_default_mappings",
    "-Xiwyu", "--max_line_length=120",
    "-Xiwyu", "--no_fwd_decls",
]

# Compiler flags that IWYU does not understand and should be stripped
STRIP_FLAG_PATTERNS = [
    re.compile(r"-mbranch-protection=\S+"),
    re.compile(r"-fstack-protector\S*"),
    re.compile(r"-Wformat\b"),
    re.compile(r"-Wformat-security\b"),
    re.compile(r"-Werror=format-security\b"),
    re.compile(r"-Winvalid-pch\b"),
    re.compile(r"-MD\b"),
    re.compile(r"-fdiagnostics-color=\S+"),
]

# Flags that take the *next* token as their argument (so we skip two tokens)
STRIP_FLAG_WITH_ARG = {"-MQ", "-MF", "-o"}


def find_repo_root() -> Path:
    """Walk up from this script to find the git repo root."""
    path = Path(__file__).resolve()
    for parent in [path] + list(path.parents):
        if (parent / ".git").exists():
            return parent
    return Path.cwd()


def find_compile_db(repo_root: Path) -> Optional[Path]:
    """Find the most recent compile_commands.json under repo_root."""
    try:
        result = subprocess.run(
            ["find", str(repo_root), "-name", "compile_commands.json",
             "-not", "-path", "*/node_modules/*",
             "-not", "-path", "*/.git/*"],
            capture_output=True, text=True, timeout=10,
        )
    except (subprocess.TimeoutExpired, FileNotFoundError):
        return None

    candidates = [Path(p) for p in result.stdout.strip().splitlines() if p]
    if not candidates:
        return None
    return max(candidates, key=lambda p: p.stat().st_mtime)


def load_compile_db(repo_root: Path) -> Optional[List[Dict]]:
    db_path = find_compile_db(repo_root)
    if db_path is None:
        return None
    return json.loads(db_path.read_text())


def _resolve_path(path: str, base_dir: str) -> str:
    """Resolve a potentially relative path against a base directory."""
    if os.path.isabs(path):
        return os.path.normpath(path)
    return os.path.normpath(os.path.join(base_dir, path))


def build_file_index(entries: List[Dict]) -> Dict[str, Dict]:
    """Map absolute source file path -> compile_commands entry."""
    index: Dict[str, Dict] = {}
    for entry in entries:
        directory = entry.get("directory", "")
        file_path = entry.get("file", "")
        if not file_path:
            continue
        abs_path = _resolve_path(file_path, directory)
        index[abs_path] = entry
    return index


def _absolutize_token(token: str, base_dir: str) -> str:
    """Absolutize include-path flags (-I, -isystem) relative to base_dir."""
    for prefix in ("-I", "-isystem"):
        if token.startswith(prefix) and len(token) > len(prefix):
            path = token[len(prefix):]
            if not os.path.isabs(path):
                return prefix + _resolve_path(path, base_dir)
    return token


def _find_sysroot_clang_flags(command: str) -> List[str]:
    """Extract sysroot from the command and build Clang-compatible flags.

    IWYU (Clang-based) needs explicit paths to the sysroot's GCC internal
    headers and C++ standard library when cross-compiling for aarch64.
    The GCC cross-compiler finds these implicitly, but Clang does not.
    """
    match = re.search(r"--sysroot=(\S+)", command)
    if not match:
        return []
    sysroot = Path(match.group(1))
    if not sysroot.is_dir():
        return []

    flags = []

    # GCC internal headers (stddef.h, stdarg.h, etc.)
    gcc_dirs = sorted(sysroot.glob("usr/lib/gcc/aarch64-poky-linux/*/include"))
    if gcc_dirs:
        flags.extend(["-isystem", str(gcc_dirs[-1])])
        # include-fixed has limits.h and syslimits.h
        include_fixed = gcc_dirs[-1].parent / "include-fixed"
        if include_fixed.is_dir():
            flags.extend(["-isystem", str(include_fixed)])

    # libstdc++ headers
    cxx_dirs = sorted(sysroot.glob("usr/include/c++/[0-9]*"))
    if cxx_dirs:
        cxx_base = cxx_dirs[-1]
        flags.extend(["-isystem", str(cxx_base)])
        cxx_arch = cxx_base / "aarch64-poky-linux"
        if cxx_arch.is_dir():
            flags.extend(["-isystem", str(cxx_arch)])

    return flags


def transform_command(entry: Dict, mapping_file: Path) -> List[str]:
    """Convert a GCC compile command into an IWYU invocation.

    All relative paths (include dirs, source file) are resolved to absolute
    paths so the command can run from any working directory.
    """
    command = entry.get("command", "")
    tokens = shlex.split(command)
    if not tokens:
        return []

    base_dir = entry.get("directory", ".")

    # Replace the compiler with include-what-you-use
    result = ["include-what-you-use"]

    # Add target flag if not already present (needed for cross-compilation)
    if "--target" not in command and "--target=" not in command:
        result.append("--target=aarch64-poky-linux")

    # Add sysroot C++ / GCC internal include paths for Clang
    result.extend(_find_sysroot_clang_flags(command))

    # Add mapping file
    result.extend(["-Xiwyu", f"--mapping_file={mapping_file}"])

    # Add standard IWYU flags
    result.extend(IWYU_FLAGS)

    # Process remaining compiler flags (skip the compiler name, first token)
    skip_next = False
    for token in tokens[1:]:
        if skip_next:
            skip_next = False
            # This is the argument to a stripped flag — but handle -isystem
            # specially: the previous token was "-isystem" as a standalone flag
            continue

        # Strip flags IWYU doesn't understand
        should_strip = False
        for pattern in STRIP_FLAG_PATTERNS:
            if pattern.fullmatch(token):
                should_strip = True
                break
        if should_strip:
            continue

        # Strip flags that take the next token as argument
        if token in STRIP_FLAG_WITH_ARG:
            skip_next = True
            continue

        # Strip -o (output file)
        if token.startswith("-o"):
            if token == "-o":
                skip_next = True
            continue

        # Handle -isystem as a separate flag followed by a path argument
        if token == "-isystem":
            result.append(token)
            # The next token is the path — don't skip, but absolutize it
            # We handle this by NOT setting skip_next, so the next token
            # goes through normal processing and gets absolutized
            continue

        # Absolutize include paths and source file path
        if token.startswith("-I") or token.startswith("-isystem"):
            result.append(_absolutize_token(token, base_dir))
        elif token.startswith("-"):
            result.append(token)
        elif token.startswith("--"):
            result.append(token)
        else:
            # Likely a source file path or a path argument to -isystem
            result.append(_resolve_path(token, base_dir))

    return result


def run_iwyu_on_file(
    file_path: str,
    entry: Dict,
    mapping_file: Path,
) -> Tuple[bool, str]:
    """Run IWYU on a single file. Returns (passed, output)."""
    cmd = transform_command(entry, mapping_file)
    if not cmd:
        return True, ""

    try:
        proc = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=60,
        )
    except subprocess.TimeoutExpired:
        return False, f"{file_path}: IWYU timed out\n"
    except FileNotFoundError:
        return True, "Warning: include-what-you-use not found in PATH\n"

    # IWYU writes diagnostics to stderr. Negative return codes indicate
    # signals (e.g. -6 = SIGABRT from a crash) — skip these files.
    output = proc.stderr
    if proc.returncode < 0:
        return True, ""

    # Check if there are actual "should add" or "should remove" suggestions.
    # Note: IWYU 0.18+ may return exit code 0 even with suggestions, so we
    # always check the output content rather than relying on the exit code.
    has_suggestions = (
        "should add these lines:" in output
        or "should remove these lines:" in output
    )
    if not has_suggestions:
        return True, ""

    # Filter out "should remove" blocks that are empty (just the header line)
    has_real_removals = False
    for line in output.split("\n"):
        if line.startswith("- "):
            has_real_removals = True
            break

    has_real_additions = "should add these lines:" in output
    # Check the line after "should add" isn't empty
    if has_real_additions:
        lines = output.split("\n")
        for i, line in enumerate(lines):
            if "should add these lines:" in line:
                # Check if next non-empty line is an include suggestion
                has_real_additions = False
                for next_line in lines[i + 1:]:
                    stripped = next_line.strip()
                    if not stripped:
                        continue
                    if stripped.startswith("#include") or stripped.startswith("//"):
                        has_real_additions = True
                    break
                break

    if not has_real_additions and not has_real_removals:
        return True, ""

    return False, output


def main() -> int:
    repo_root = find_repo_root()
    mapping_file = repo_root / ".iwyu.imp"

    if not mapping_file.exists():
        print(f"Warning: IWYU mapping file not found: {mapping_file}", file=sys.stderr)
        mapping_file_arg = None
    else:
        mapping_file_arg = mapping_file

    # Check that IWYU is installed
    if not shutil.which("include-what-you-use"):
        print("Warning: include-what-you-use not found — skipping IWYU check", file=sys.stderr)
        print("  Install with: apt install iwyu", file=sys.stderr)
        return 0

    # Load compilation database
    entries = load_compile_db(repo_root)
    if entries is None:
        print(
            "Warning: compile_commands.json not found — skipping IWYU check\n"
            "  Build the project first, then run merge_compile_commands_util.py",
            file=sys.stderr,
        )
        return 0

    file_index = build_file_index(entries)

    # Files to check come from command-line args (pre-commit passes them)
    files = sys.argv[1:]
    if not files:
        return 0

    mapping = mapping_file_arg or Path("/dev/null")
    tasks = []
    for filepath in files:
        abs_path = str(Path(filepath).resolve())
        if abs_path in file_index:
            tasks.append((filepath, abs_path, file_index[abs_path]))

    if not tasks:
        return 0

    failed_files: List[Tuple[str, str]] = []
    max_workers = min(len(tasks), max(1, (os.cpu_count() or 4) // 2))
    with ThreadPoolExecutor(max_workers=max_workers) as pool:
        futures = {
            pool.submit(run_iwyu_on_file, abs_path, entry, mapping): filepath
            for filepath, abs_path, entry in tasks
        }
        for future in as_completed(futures):
            filepath = futures[future]
            passed, output = future.result()
            if not passed:
                failed_files.append((filepath, output))

    if failed_files:
        print("=" * 70, file=sys.stderr)
        print("IWYU: the following files have include issues:", file=sys.stderr)
        print("=" * 70, file=sys.stderr)
        for filepath, output in failed_files:
            print(f"\n--- {filepath} ---", file=sys.stderr)
            print(output, file=sys.stderr)
        print("=" * 70, file=sys.stderr)
        print(
            "Fix includes manually or run:  fix_include < iwyu_output",
            file=sys.stderr,
        )
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
