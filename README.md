# cslox

Learning repository: [Crafting Interpreters](https://craftinginterpreters.com)

This repository contains two Lox implementations from Robert Nystrom's *Crafting Interpreters*:

| Project | Language | Description |
|---------|----------|-------------|
| `cslox` | C# (.NET 10) | Tree-walk interpreter (Part II) |
| `clox`  | C          | Bytecode VM (Part III) |

---

## Prerequisites

| Tool | Required by | Version |
|------|-------------|---------|
| [.NET SDK](https://dotnet.microsoft.com/en-us/download) | cslox | **10.0** (or later) |
| [CMake](https://cmake.org/download/) | clox | **3.19** (or later) |
| A C compiler | clox | MSVC, GCC, or Clang |
| [Ninja](https://ninja-build.org/) | clox (optional) | Any recent |
| [Python](https://www.python.org/downloads/) | tests | **2.7** or **3.x** (scripts are compatible with both) |

---

## Building cslox (C# tree-walk interpreter)

```bash
# From the repository root:

# Build
dotnet build cslox.sln

# Or build just the project directly:
dotnet build src/cslox/cslox.csproj
```

**Run a Lox script:**

```bash
dotnet run --project src/cslox/cslox.csproj -- test/assignment/global.lox
```

Or run the compiled DLL directly:

```bash
# Windows (PowerShell / cmd)
dotnet src/cslox/bin/Debug/net10.0/cslox.dll test/assignment/global.lox

# macOS / Linux
dotnet src/cslox/bin/Debug/net10.0/cslox.dll test/assignment/global.lox
```

---

## Building clox (C bytecode VM)

clox uses CMake. Choose the generator that matches your environment.

### Option A: Ninja (recommended, cross-platform)

```bash
# Configure
cmake -B build -G Ninja -S .

# Build (Debug)
cmake --build build

# Run
./build/lox          # macOS / Linux
build\Debug\lox.exe  # Windows
```

### Option B: Visual Studio / MSBuild (Windows)

```bash
# Configure (CMake auto-detects Visual Studio)
cmake -B build -S .

# Build (Debug)
cmake --build build

# Run
build\Debug\lox.exe
```

### Option C: Make / Unix Makefiles (macOS / Linux)

```bash
cmake -B build -G "Unix Makefiles" -S .
cmake --build build
./build/lox
```

> **Note:** CMake auto-detects your C compiler. Set `CC` environment variable or use
> `-DCMAKE_C_COMPILER=` to override (e.g., `-DCMAKE_C_COMPILER=clang`).

---

## Running Tests

The test suite validates both interpreters against expected output embedded in `.lox` test files.

```bash
# Run all tests with clox (default) — builds automatically first
python tool/run_tests.py

# Run all tests with cslox (Release build)
python tool/run_tests.py --interpreter cslox

# Run only tests matching a pattern
python tool/run_tests.py --filter "assignment/*"

# Run with verbose output
python tool/run_tests.py --verbose

# Skip auto-build if already built
python tool/run_tests.py --no-build

# Include benchmark and stress tests
python tool/run_tests.py --include-benchmark

# Set a per-test timeout (seconds, default: 30; 0 = none)
python tool/run_tests.py --timeout 10
```

| Flag | Values | Default | Description |
|------|--------|---------|-------------|
| `--interpreter` | `clox`, `cslox` | `clox` | Which interpreter to test |
| `--filter`, `-f` | glob pattern | *(all)* | Run only matching test files |
| `--verbose`, `-v` | — | off | Print each test result individually |
| `--no-build` | — | off | Skip building before running tests |
| `--include-benchmark` | — | off | Include `benchmark/` and `limit/` tests |
| `--include-scanning` | — | off | Include `scanning/` tests |
| `--timeout` | seconds | `30` | Per-test timeout (0 = no limit) |

**Build details:**
- **clox** builds via `cmake --build build/TestRelease` — configure first with:
  ```bash
  # Windows (Visual Studio x64)
  cmake -S . -B build/TestRelease -DCMAKE_BUILD_TYPE=Release -A x64

  # macOS / Linux
  cmake -S . -B build/TestRelease -DCMAKE_BUILD_TYPE=Release
  ```
- **cslox** builds via `dotnet build -c Release`

Test files are located under `test/`, organized by feature (assignment, closure, class, function, etc.).
