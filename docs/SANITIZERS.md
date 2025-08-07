# Sanitizer Testing Guide

## Overview

slwoggy uses multiple sanitizers to detect memory errors, data races, and undefined behavior. Sanitizer tests run automatically on every PR and can be run locally before committing.

## Available Sanitizers

| Sanitizer | Flag | Detects |
|-----------|------|---------|
| AddressSanitizer (ASan) | `address` | Buffer overflows, use-after-free, memory leaks |
| ThreadSanitizer (TSan) | `thread` | Data races, threading issues |
| UndefinedBehaviorSanitizer (UBSan) | `undefined` | Integer overflow, null derefs, type confusion |
| MemorySanitizer (MSan) | `memory` | Uninitialized memory reads (Clang/Linux only) |

## Quick Start

### Run default sanitizers (ASan + UBSan)
```bash
./sanitize.sh
```

### Run with tests
```bash
./sanitize.sh address,undefined test
```

### Run specific sanitizer
```bash
./sanitize.sh thread test
```

### Run all sanitizers
```bash
./run-sanitizers.sh
```

## Pre-commit Hook

A pre-commit hook automatically runs quick sanitizer checks:
```bash
# Already installed in .git/hooks/pre-commit
# Runs ASan+UBSan on build
# Skips on CI to avoid duplicate checks
```

## CI/CD Integration

GitHub Actions runs sanitizers on every push and PR:
- **Linux**: ASan+UBSan, TSan, MSan
- **macOS**: ASan+UBSan (no leak detection)
- Results posted as PR comments
- Artifacts uploaded for failed runs

## Manual Testing

### Using CMake directly
```bash
# Configure with sanitizer
cmake -B build/san -DSANITIZER=address,undefined

# Build
cmake --build build/san

# Run with environment variables
ASAN_OPTIONS=detect_leaks=1:halt_on_error=0 ./build/san/bin/slwoggy
```

### Environment Variables

#### AddressSanitizer
```bash
export ASAN_OPTIONS=detect_leaks=1:check_initialization_order=1:strict_init_order=1:halt_on_error=0
```

#### ThreadSanitizer
```bash
export TSAN_OPTIONS=halt_on_error=0:history_size=7
```

#### UndefinedBehaviorSanitizer
```bash
export UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=0
```

#### MemorySanitizer
```bash
export MSAN_OPTIONS=halt_on_error=0
```

## Reading Reports

### Local Reports
```bash
# Reports saved in sanitizer-reports/
ls sanitizer-reports/
cat sanitizer-reports/summary_*.txt
```

### CI Reports
- Check workflow logs in Actions tab
- Download artifacts for detailed reports
- PR comments show summary

## Suppression Files

All sanitizers are configured to ignore errors in third-party libraries and system libraries. Suppression files are located in the `sanitizers/` directory:

- `sanitizers/asan.supp` - AddressSanitizer suppressions
- `sanitizers/ubsan.supp` - UndefinedBehaviorSanitizer suppressions
- `sanitizers/tsan.supp` - ThreadSanitizer suppressions
- `sanitizers/msan.supp` - MemorySanitizer suppressions

These files automatically exclude:
- All third-party libraries (`third_party/*`)
- System libraries (`/usr/lib/*`, `/lib/*`, `/System/*`)
- Standard library code (`std::*`, `pthread_*`)

Suppressions are automatically applied by:
- CMake configuration
- All helper scripts (`sanitize.sh`, `run-sanitizers.sh`)
- Pre-commit hooks
- GitHub Actions CI

To add custom suppressions, edit the appropriate file in `sanitizers/`.

## Common Issues

### macOS Leak Detection
macOS doesn't support leak detection in ASan. Use Instruments or Valgrind on Linux instead.

### TSan Incompatibility
ThreadSanitizer cannot be used with AddressSanitizer or MemorySanitizer. Run separately.

### False Positives
The default suppression files should handle most false positives. If you encounter issues with specific libraries, add them to the suppression files:
```bash
# Add to sanitizers/asan.supp
echo "leak:problematic_library/*" >> sanitizers/asan.supp
```

### Performance Impact
Sanitizers slow down execution:
- ASan: 2x slowdown
- TSan: 5-15x slowdown
- MSan: 3x slowdown

## Best Practices

1. **Development**: Use `./sanitize.sh` for quick checks
2. **Pre-commit**: Let the hook run automatically
3. **CI**: Review PR comments and artifacts
4. **Release**: Run full suite with `./run-sanitizers.sh`

## Troubleshooting

### Build Fails with Sanitizer
```bash
# Clean build directory
rm -rf build/san
# Retry with verbose output
cmake -B build/san -DSANITIZER=address --debug-output
```

### Sanitizer Not Detecting Issues
```bash
# Ensure halt_on_error=0 to see all issues
export ASAN_OPTIONS=halt_on_error=0:print_stats=1
# Check if optimizations are disabled
cmake -DCMAKE_BUILD_TYPE=Debug
```

### CI Sanitizer Timeouts
Increase timeout in `.github/workflows/sanitizers.yml`:
```yaml
timeout-minutes: 30  # Default is 10
```