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

The test suite validates the C# interpreter (cslox) against expected output embedded in `.lox` test files.

```bash
# Run all tests (builds cslox automatically first)
python tool/run_tests.py

# Run only tests matching a pattern
python tool/run_tests.py --filter "assignment/*"

# Run with verbose output
python tool/run_tests.py --verbose

# Skip auto-build if already built
python tool/run_tests.py --no-build
```

Test files are located under `test/`, organized by feature (assignment, closure, class, function, etc.).
