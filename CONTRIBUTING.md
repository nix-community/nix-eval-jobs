# Contributing to nix-eval-jobs

Thank you for considering contributing to nix-eval-jobs! This document provides
guidelines and instructions for contributing to the project.

## Development Setup

1. Clone the repository:
   ```bash
   git clone https://github.com/nix-community/nix-eval-jobs.git
   cd nix-eval-jobs
   ```

2. Set up the development environment:
   ```bash
   # Using nix
   nix develop
   # Or using direnv
   direnv allow
   ```

## Building and Testing

### Building

```bash
meson setup build
cd build
ninja
```

### Running Tests

```bash
pytest ./tests
```

### Checking Everything

To run all builds, tests, and checks:

```bash
nix flake check
```

This will:

- Build the package for all supported platforms
- Run the test suite
- Run all formatters and linters
- Perform static analysis checks

## Code Quality Tools

### Formatting

- Clang-format for C++ code
- Ruff for Python code formatting and linting
- Deno and yamlfmt for YAML files
- nixfmt for Nix files

### Static Analysis

- MyPy for Python type checking
- deadnix for Nix code analysis
- clang-tidy for C++ code analysis
  ```bash
  # Run clang-tidy checks
  ninja clang-tidy
  # Auto-fix clang-tidy issues where possible
  ninja clang-tidy-fix
  ```

All formatting can be applied using:

```bash
nix fmt
```

## Making Changes

1. Create a branch for your changes:
   ```bash
   git checkout -b your-feature-name
   ```

2. Make your changes and commit them with descriptive commit messages:
   ```bash
   git commit -m "feat: Add new feature X"
   ```

3. Push your changes to your fork:
   ```bash
   git push origin your-feature-name
   ```

4. Create a Pull Request against the main repository.

## Additional Resources

- [Project README](README.md)
