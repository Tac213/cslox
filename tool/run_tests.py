#!/usr/bin/env python
# -*- coding: utf-8 -*-
# author: Tac
# contact: cookiezhx@163.com

"""
Test runner for cslox.
Iterates over all .lox files under test/,
runs them through the cslox interpreter, and validates output
against expected results embedded in comments.

https://craftinginterpreters.com/

Expected comment patterns in .lox files:
    // expect: <output>              — expected stdout line
    // expect runtime error: <msg>   — expected runtime error
    // [line N] Error at 'X': <msg>  — expected compile error
    // [line N] Error: <msg>         — expected compile error (without token)
    // Error at 'X': <msg>           — expected compile error (any line)
    // Error: <msg>                  — expected compile error (any line)

Usage:
    python tool/run_tests.py [--filter PATTERN] [--verbose] [--no-build]
                             [--include-benchmark] [--include-scanning]
                             [--timeout SECONDS]
"""

# pylint: disable=missing-function-docstring,consider-using-f-string,line-too-long

from __future__ import absolute_import, division, print_function, unicode_literals

import sys
import os
import re
import io
import time
import subprocess
import fnmatch
try:
    from typing import TYPE_CHECKING
except ImportError:
    TYPE_CHECKING = False

if TYPE_CHECKING:
    from typing import List, Tuple, Optional, Dict, Iterable, TextIO, Union


# ---------------------------------------------------------------------------
# Python 2/3 compatibility helpers
# ---------------------------------------------------------------------------

def _is_python3():
    # type: () -> bool
    return sys.version_info[0] >= 3


# ---------------------------------------------------------------------------
# Expected-output parser
# ---------------------------------------------------------------------------

_EXPECT_PATTERN = re.compile(r'//\s*expect:\s*(.*)')
_EXPECT_RUNTIME_ERROR_PATTERN = re.compile(r'//\s*expect\s+runtime\s+error:\s*(.*)')
# Generic compile error: [line N] Error at 'X': message  OR  [line N] Error: message  OR  [line N] Error at end: message
_LINE_ERROR_PATTERN = re.compile(
    r'//\s*\[line\s+(\d+)\]\s*Error\s*(?:at\s+(?:\'([^\']*)\'|(end)))?\s*:\s*(.*)'
)
# Bare error (no line number): Error at 'X': message  OR  Error: message  OR  Error at end: message
_BARE_ERROR_PATTERN = re.compile(
    r'//\s*Error\s*(?:at\s+(?:\'([^\']*)\'|(end)))?\s*:\s*(.*)'
)


class ExpectedResult(object):
    """Parsed expected output/errors for a single .lox test file."""

    def __init__(self):
        # type: () -> None
        self.stdout_lines = []       # type: List[str]
        self.runtime_error = None    # type: Optional[str]
        self.compile_errors = []     # type: List[Tuple[Optional[int], Optional[str], str]]
        # Each tuple: (line_number_or_None, token_lexeme_or_None, message)

    @property
    def expects_success(self):
        # type: () -> bool
        """True if the test expects clean execution (no errors)."""
        return not self.compile_errors and self.runtime_error is None

    @property
    def expects_compile_error(self):
        # type: () -> bool
        return bool(self.compile_errors)

    @property
    def expects_runtime_error(self):
        # type: () -> bool
        return self.runtime_error is not None

    @property
    def expects_output(self):
        # type: () -> bool
        return bool(self.stdout_lines)


def parse_expected(filepath):
    # type: (str) -> ExpectedResult
    """Parse a .lox file and extract expected output/errors from comments."""
    result = ExpectedResult()

    raw_generic_errors = []  # type: List[Tuple[Optional[int], Optional[str], str]]

    with io.open(filepath, 'r', encoding='utf-8') as fp:
        for raw_line in fp:
            line = raw_line.rstrip('\n').rstrip('\r')

            # Check for // expect: <output>
            m = _EXPECT_PATTERN.search(line)
            if m:
                result.stdout_lines.append(m.group(1))
                continue

            # Check for // expect runtime error: <message>
            m = _EXPECT_RUNTIME_ERROR_PATTERN.search(line)
            if m:
                result.runtime_error = m.group(1)
                continue

            # Check for generic error: [line N] Error...
            m = _LINE_ERROR_PATTERN.search(line)
            if m:
                line_no = int(m.group(1))
                token = m.group(2) or m.group(3)  # 'X' or 'end'
                message = m.group(4)
                raw_generic_errors.append((line_no, token, message))
                continue

            # Check for bare error: Error at 'X': ...  or  Error: ...
            # (Only match if not already matched by other error/expect patterns)
            m = _BARE_ERROR_PATTERN.search(line)
            if m:
                token = m.group(1) or m.group(2)  # 'X' or 'end'
                message = m.group(3)
                raw_generic_errors.append((None, token, message))
                continue

    result.compile_errors = raw_generic_errors

    return result


# ---------------------------------------------------------------------------
# Interpreter runner
# ---------------------------------------------------------------------------

def build_project(project_dir):
    # type: (str) -> bool
    """Run 'dotnet build' for the cslox project. Returns True on success."""
    print("Building cslox...")
    try:
        ret = subprocess.call(
            ['dotnet', 'build', project_dir, '--nologo', '-v', 'q'],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        if ret == 0:
            print("Build succeeded.\n")
            return True
        else:
            print("Build failed (exit code {}).".format(ret))
            return False
    except OSError as e:
        print("Failed to run 'dotnet build': {}".format(e))
        return False


def find_dotnet_dll(project_dir):
    # type: (str) -> Optional[str]
    """Find the compiled cslox DLL under the project's output directory."""
    for root, _, files in os.walk(os.path.join(project_dir, 'bin')):
        for f in files:
            if f == 'cslox.dll':
                return os.path.join(root, f)
    return None


def run_lox_file(dll_path, lox_file, timeout=None):
    # type: (str, str, Optional[int]) -> Tuple[int, str, str, bool]
    """
    Run a .lox file through the cslox interpreter.
    Returns (exit_code, stdout_text, stderr_text, timed_out).
    If timeout is given (in seconds), the process is killed if it exceeds it.
    """
    try:
        proc = subprocess.Popen(
            ['dotnet', dll_path, lox_file],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        timed_out = False
        if timeout is not None and hasattr(subprocess, 'TimeoutExpired'):
            try:
                out_bytes, err_bytes = proc.communicate(timeout=timeout)
            except subprocess.TimeoutExpired:
                proc.kill()
                out_bytes, err_bytes = proc.communicate()
                timed_out = True
        else:
            out_bytes, err_bytes = proc.communicate()

        exit_code = proc.returncode

        if _is_python3():
            stdout_text = out_bytes.decode('utf-8', errors='replace') if out_bytes else ''
            stderr_text = err_bytes.decode('utf-8', errors='replace') if err_bytes else ''
        else:
            stdout_text = out_bytes if out_bytes else ''
            stderr_text = err_bytes if err_bytes else ''

        return exit_code, stdout_text, stderr_text, timed_out
    except OSError as e:
        return -1, '', 'Failed to run: {}'.format(e), False


# ---------------------------------------------------------------------------
# Output comparison
# ---------------------------------------------------------------------------

# Regex to match a compile error line from cslox stderr:
#   [line N] Error at 'X': message
#   [line N] Error: message
#   [line N] Error at end: message
_CLOX_ERROR_LINE_PATTERN = re.compile(
    r'\[line\s+(\d+)\]\s*Error\s*(?:at\s+(?:\'([^\']*)\'|(end)))?\s*:\s*(.*)'
)

# Regex to match a runtime error from cslox stderr:
#   message
#   [line N]
_CLOX_RUNTIME_ERROR_PATTERN = re.compile(
    r'^(.*)\n\[line\s+(\d+)\]', re.DOTALL
)


def check_result(filepath, expected, exit_code, stdout_text, stderr_text):  # pylint: disable=unused-argument
    # type: (str, ExpectedResult, int, str, str) -> Tuple[bool, List[str]]
    """
    Compare actual output against expected.
    Returns (passed, list_of_failure_messages).
    """
    failures = []  # type: List[str]

    # Normalize line endings for comparison
    stdout_text = stdout_text.replace('\r\n', '\n').replace('\r', '\n')
    stderr_text = stderr_text.replace('\r\n', '\n').replace('\r', '\n')

    # 1. Check exit code
    if expected.expects_compile_error:
        if exit_code != 65:
            failures.append(
                "Expected compile error exit code 65, got {}".format(exit_code)
            )
    elif expected.expects_runtime_error:
        if exit_code != 70:
            failures.append(
                "Expected runtime error exit code 70, got {}".format(exit_code)
            )
    else:
        if exit_code not in (0, None):
            failures.append(
                "Expected exit code 0, got {}".format(exit_code)
            )

    # 2. Check stdout
    if expected.expects_output:
        actual_lines = [l for l in stdout_text.split('\n') if l or expected.stdout_lines]
        # Strip trailing empty lines for comparison
        while actual_lines and actual_lines[-1] == '':
            actual_lines.pop()

        expected_lines = expected.stdout_lines

        if len(actual_lines) != len(expected_lines):
            failures.append(
                "Expected {} stdout lines, got {} lines".format(
                    len(expected_lines), len(actual_lines)
                )
            )
            if actual_lines:
                failures.append("  Actual stdout:")
                for line in actual_lines:
                    failures.append("    | {}".format(line))
            if expected_lines:
                failures.append("  Expected stdout:")
                for line in expected_lines:
                    failures.append("    | {}".format(line))
        else:
            for i, (actual, exp) in enumerate(zip(actual_lines, expected_lines)):
                if actual != exp:
                    failures.append(
                        "stdout line {} mismatch:\n"
                        "  Expected: {}\n"
                        "  Actual:   {}".format(i + 1, repr(exp), repr(actual))
                    )

    # 3. Check compile errors
    if expected.expects_compile_error:
        # Parse actual error lines from stderr
        actual_errors = []  # type: List[Tuple[Optional[int], Optional[str], str]]
        for raw_line in stderr_text.split('\n'):
            line = raw_line.strip()
            if not line:
                continue
            m = _CLOX_ERROR_LINE_PATTERN.match(line)
            if m:
                line_no = int(m.group(1))
                token = m.group(2) or m.group(3)  # 'X' or 'end'
                message = m.group(4)
                actual_errors.append((line_no, token, message))
                continue
            # If it doesn't match the error pattern but stderr is non-empty, note it
            if 'Error' in line or 'Warning' in line:
                # Try to capture as unknown error format
                actual_errors.append((None, None, line))

        if not actual_errors:
            failures.append(
                "Expected compile errors but stderr was empty or had no recognizable errors.\n"
                "  stderr: {}".format(repr(stderr_text))
            )
        else:
            # Check each expected error against actual errors
            for exp_line_no, exp_token, exp_msg in expected.compile_errors:
                found = False
                for act_line_no, act_token, act_msg in actual_errors:
                    # Check line number (if expected)
                    if exp_line_no is not None and act_line_no is not None:
                        if exp_line_no != act_line_no:
                            continue
                    # Check token (if expected)
                    if exp_token is not None and act_token is not None:
                        if exp_token != act_token:
                            continue
                    # Check message (substring match)
                    if exp_msg in act_msg:
                        found = True
                        break
                if not found:
                    failures.append(
                        "Expected error not found: {}".format(
                            _format_expected_error(exp_line_no, exp_token, exp_msg)
                        )
                    )
            if failures:
                failures.append("  Actual stderr:")
                for line in stderr_text.split('\n'):
                    if line.strip():
                        failures.append("    | {}".format(line))

    # 4. Check runtime error
    if expected.expects_runtime_error:
        m = _CLOX_RUNTIME_ERROR_PATTERN.search(stderr_text)
        if not m:
            failures.append(
                "Expected runtime error '{}' but no runtime error found in stderr.\n"
                "  stderr: {}".format(expected.runtime_error, repr(stderr_text))
            )
        else:
            actual_msg = m.group(1).strip()
            if expected.runtime_error not in actual_msg:
                failures.append(
                    "Runtime error message mismatch:\n"
                    "  Expected: {}\n"
                    "  Actual:   {}".format(
                        repr(expected.runtime_error), repr(actual_msg)
                    )
                )

    # 5. If we expect neither errors nor output, just check success
    if not expected.expects_output and not expected.expects_compile_error and not expected.expects_runtime_error:
        # This is a test that expects to run cleanly with no output
        if exit_code != 0:
            failures.append(
                "Expected clean execution (exit 0), got exit {}.\n"
                "  stderr: {}".format(exit_code, repr(stderr_text.strip()))
            )

    passed = len(failures) == 0
    return passed, failures


def _format_expected_error(line_no, token, message):
    # type: (Optional[int], Optional[str], str) -> str
    parts = []
    if line_no is not None:
        parts.append("[line {}]".format(line_no))
    parts.append("Error")
    if token is not None:
        parts.append("at '{}'".format(token))
    parts.append(": {}".format(message))
    return ' '.join(parts)


# ---------------------------------------------------------------------------
# File discovery
# ---------------------------------------------------------------------------

def find_lox_files(test_dir, skip_dirs=None):
    # type: (str, Optional[Iterable[str]]) -> List[str]
    """Recursively find all .lox files under test_dir, excluding skip_dirs."""
    if skip_dirs is None:
        skip_dirs = frozenset()

    lox_files = []
    skip_dirs_normalized = frozenset(os.path.normpath(d) for d in skip_dirs)

    for root, dirs, files in os.walk(test_dir):
        # Filter out skipped directories
        dirs[:] = [
            d for d in dirs
            if os.path.normpath(os.path.join(root, d)) not in skip_dirs_normalized
        ]

        for f in files:
            if f.endswith('.lox'):
                lox_files.append(os.path.join(root, f))

    lox_files.sort()
    return lox_files


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    # type: () -> None
    """Entry point."""

    # --- Parse arguments ---
    verbose = False
    no_build = False
    include_benchmark = False
    include_scanning = False
    timeout = 30  # type: Optional[int]  # default 30-second timeout per test
    filter_pattern = None  # type: Optional[str]
    args = sys.argv[1:]

    i = 0
    while i < len(args):
        arg = args[i]
        if arg == '--verbose' or arg == '-v':
            verbose = True
        elif arg == '--no-build':
            no_build = True
        elif arg == '--include-benchmark':
            include_benchmark = True
        elif arg == '--include-scanning':
            include_scanning = True
        elif arg == '--timeout':
            i += 1
            if i < len(args):
                try:
                    timeout = int(args[i])
                except ValueError:
                    print("Error: --timeout requires an integer (seconds).", file=sys.stderr)
                    sys.exit(64)
                if timeout <= 0:
                    timeout = None  # 0 or negative means no timeout
            else:
                print("Error: --timeout requires a value (seconds).", file=sys.stderr)
                sys.exit(64)
        elif arg == '--filter' or arg == '-f':
            i += 1
            if i < len(args):
                filter_pattern = args[i]
            else:
                print("Error: --filter requires a pattern argument.", file=sys.stderr)
                sys.exit(64)
        elif arg == '--help' or arg == '-h':
            print(__doc__)
            sys.exit(0)
        else:
            print("Unknown argument: {}".format(arg), file=sys.stderr)
            print("Usage: python tool/run_tests.py [--filter PATTERN] [--verbose] [--no-build] [--include-benchmark] [--include-scanning] [--timeout SECONDS]", file=sys.stderr)
            sys.exit(64)
        i += 1

    # --- Locate directories ---
    script_dir = os.path.dirname(os.path.abspath(__file__))
    repo_root = os.path.normpath(os.path.join(script_dir, '..'))
    project_dir = os.path.join(repo_root, 'src', 'cslox')
    test_dir = os.path.join(repo_root, 'test')
    scanning_dir = os.path.join(test_dir, 'scanning')

    if not os.path.isdir(project_dir):
        print("Error: Project directory not found: {}".format(project_dir), file=sys.stderr)
        sys.exit(64)
    if not os.path.isdir(test_dir):
        print("Error: Test directory not found: {}".format(test_dir), file=sys.stderr)
        sys.exit(64)

    # --- Build ---
    dll_path = find_dotnet_dll(project_dir)
    if not no_build or not dll_path:
        if not build_project(project_dir):
            sys.exit(1)
        dll_path = find_dotnet_dll(project_dir)
        if not dll_path:
            print("Error: Could not find compiled cslox.dll after build.", file=sys.stderr)
            sys.exit(1)
    else:
        print("Skipping build (--no-build), using: {}\n".format(dll_path))

    # --- Find test files ---
    skip_dirs = []
    if not include_scanning:
        skip_dirs.append(scanning_dir)
    if not include_benchmark:
        skip_dirs.append(os.path.join(test_dir, 'benchmark'))
        skip_dirs.append(os.path.join(test_dir, 'limit'))

    lox_files = find_lox_files(test_dir, skip_dirs=skip_dirs)

    if filter_pattern:
        lox_files = [f for f in lox_files if fnmatch.fnmatch(
            os.path.relpath(f, test_dir).replace('\\', '/'), filter_pattern
        )]

    if not lox_files:
        print("No .lox test files found.")
        sys.exit(0)

    if timeout is not None:
        print("Per-test timeout: {}s".format(timeout))
    if not include_benchmark:
        print("Skipping benchmark/ and limit/ (use --include-benchmark to run them)")
    if not include_scanning:
        print("Skipping scanning/ (use --include-scanning to run them)")
    print("Found {} test file(s).\n".format(len(lox_files)))

    # --- Run tests ---
    passed = 0
    failed = 0
    skipped = 0
    failures = []  # type: List[Tuple[str, List[str]]]

    start_time = time.time()

    for i, lox_file in enumerate(lox_files):
        rel_path = os.path.relpath(lox_file, repo_root)

        # Parse expected results
        try:
            expected = parse_expected(lox_file)
        except Exception as e:  # pylint: disable=broad-exception-caught
            print("  SKIP  {} (parse error: {})".format(rel_path, e))
            skipped += 1
            continue

        # Run the interpreter
        exit_code, stdout_text, stderr_text, timed_out = run_lox_file(
            dll_path, lox_file, timeout=timeout
        )

        # Check results
        if timed_out:
            test_passed = False
            failure_msgs = ["Test timed out after {} seconds.".format(timeout)]
        else:
            test_passed, failure_msgs = check_result(
                lox_file, expected, exit_code, stdout_text, stderr_text
            )

        if test_passed:
            passed += 1
            if verbose:
                print("  PASS  {}".format(rel_path))
            else:
                # Print a dot for progress
                sys.stdout.write('.')
                sys.stdout.flush()
        else:
            failed += 1
            failures.append((rel_path, failure_msgs))
            if verbose:
                print("  FAIL  {}".format(rel_path))
                for msg in failure_msgs:
                    for line in msg.split('\n'):
                        print("        {}".format(line))
            else:
                sys.stdout.write('F')
                sys.stdout.flush()

    elapsed = time.time() - start_time

    # --- Summary ---
    if not verbose:
        print()  # newline after dots
    print()
    print("=" * 60)
    print("Test results: {} passed, {} failed, {} skipped".format(passed, failed, skipped))
    print("Time: {:.2f}s".format(elapsed))
    print("=" * 60)

    if failures:
        print()
        print("FAILURES:")
        print("-" * 60)
        for rel_path, failure_msgs in failures:
            print()
            print("  {}".format(rel_path))
            for msg in failure_msgs:
                for line in msg.split('\n'):
                    print("    {}".format(line))

        sys.exit(1)
    else:
        print()
        print("All tests passed!")
        sys.exit(0)


if __name__ == '__main__':
    main()
