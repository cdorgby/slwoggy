# slwoggy Documentation

This directory contains technical design documentation and implementation notes for the slwoggy logging library.

## Contents

### Design Documents

- [ROTATION_DESIGN.md](ROTATION_DESIGN.md) - Comprehensive design document for the file rotation service, including:
  - Architecture decisions and rationale
  - Implementation requirements and constraints
  - Clock domain correctness considerations
  - File descriptor management strategies
  - Data integrity trade-offs
  - ENOSPC handling approach
  - Zero-gap rotation implementation
  - Performance considerations

## User Documentation

For user-facing documentation, please see:
- [Main README](../README.md) - Getting started, features, and usage examples
- [API Documentation](../include/) - Header files with Doxygen documentation
- [Examples](../src/) - Demo applications showing various features

## Contributing

When adding new features, please include:
1. Design document explaining the architecture and key decisions
2. User documentation in the main README
3. API documentation in header files
4. Example code demonstrating the feature