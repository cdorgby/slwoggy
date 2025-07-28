# Raylib Hello World Cross-Platform Project

A simple Hello World application using raylib that can be compiled for Linux, Windows (via MinGW), and macOS.

## Table of Contents
- [Development Environment Setup](#development-environment-setup)
  - [Windows (WSL)](#windows-wsl)
  - [Linux](#linux)
  - [macOS](#macos)
- [Building](#building)
- [Packaging](#packaging)
- [Running](#running)
- [Project Structure](#project-structure)

## Development Environment Setup

### Windows (WSL)

1. **Install WSL2** (if not already installed):
   ```powershell
   wsl --install
   ```

2. **Install Ubuntu in WSL**:
   ```powershell
   wsl --install -d Ubuntu
   ```

3. **Update packages and install build tools**:
   ```bash
   sudo apt update && sudo apt upgrade
   sudo apt install build-essential cmake git
   ```

4. **Install MinGW for Windows cross-compilation**:
   ```bash
   sudo apt install mingw-w64
   ```

5. **Install packaging tools** (optional):
   ```bash
   # For creating Windows installers
   sudo apt install nsis
   
   # For creating Linux packages
   sudo apt install rpm
   ```

6. **Install VS Code** (on Windows) and the Remote-WSL extension

### Linux

1. **Ubuntu/Debian**:
   ```bash
   sudo apt update
   sudo apt install build-essential cmake git
   
   # For cross-compiling to Windows
   sudo apt install mingw-w64
   
   # Optional: packaging tools
   sudo apt install nsis rpm
   ```

2. **Fedora/RHEL**:
   ```bash
   sudo dnf groupinstall "Development Tools"
   sudo dnf install cmake git
   
   # For cross-compiling to Windows
   sudo dnf install mingw64-gcc mingw64-gcc-c++
   
   # Optional: packaging tools
   sudo dnf install nsis rpm-build
   ```

3. **Arch Linux**:
   ```bash
   sudo pacman -S base-devel cmake git
   
   # For cross-compiling to Windows
   sudo pacman -S mingw-w64-gcc
   
   # Optional: packaging tools
   yay -S nsis
   ```

### macOS

1. **Install Xcode Command Line Tools**:
   ```bash
   xcode-select --install
   ```

2. **Install Homebrew** (if not already installed):
   ```bash
   /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
   ```

3. **Install CMake**:
   ```bash
   brew install cmake
   ```

4. **For cross-compiling to Windows** (optional):
   ```bash
   brew install mingw-w64
   ```

## Building

### Quick Build Commands

```bash
# Build for current platform (Linux/macOS)
./build-linux.sh

# Cross-compile for Windows (from Linux/WSL)
./build-windows.sh

# Build for all platforms
./build-all.sh
```

### Manual Build Process

#### Linux/macOS Native Build
```bash
mkdir -p build/native
cd build/native
cmake ../..
make -j$(nproc)  # Linux
make -j$(sysctl -n hw.ncpu)  # macOS
```

#### Windows Cross-Compilation (from Linux/WSL)
```bash
mkdir -p build/windows
cd build/windows
cmake ../.. -DCMAKE_TOOLCHAIN_FILE=../../windows-toolchain.cmake
make -j$(nproc)
```

## Packaging

### Creating Distribution Packages

```bash
# Create Linux packages (DEB, RPM, TGZ)
./package-linux.sh

# Create Windows packages (NSIS installer, ZIP)
./package-windows.sh

# Create macOS package (DMG) - run on macOS
./package-macos.sh

# Create all packages
./package-all.sh
```

### Package Outputs

- **Linux**:
  - `build/linux/*.deb` - Debian/Ubuntu package
  - `build/linux/*.rpm` - RedHat/Fedora package
  - `build/linux/*.tar.gz` - Generic Linux archive

- **Windows**:
  - `build/windows/*.exe` - NSIS installer
  - `build/windows/*.zip` - Portable ZIP archive

- **macOS**:
  - `build/macos/*.dmg` - macOS disk image with app bundle

## Running

### Linux
```bash
./build/linux/bin/RaylibHelloWorld
```

### Windows
```bash
# From WSL/Linux (using Wine)
wine ./build/windows/bin/RaylibHelloWorld.exe

# Or copy to Windows and run natively
```

### macOS
```bash
# Command line
./build/macos/bin/RaylibHelloWorld

# Or open the app bundle
open build/macos/RaylibHelloWorld.app
```

## VS Code Integration

The project includes VS Code configuration for:

1. **Building** (`Ctrl+Shift+B`):
   - Build Linux (default)
   - Build Windows
   - Build All

2. **Debugging** (`F5`):
   - Debug Linux build with GDB

3. **Tasks** (`Ctrl+Shift+P` → "Tasks: Run Task"):
   - Various build and package tasks

### Opening in VS Code

From WSL:
```bash
code .
```

From Windows (with WSL Remote):
1. Open VS Code
2. Press `Ctrl+Shift+P`
3. Select "WSL: Open Folder in WSL..."
4. Navigate to the project directory

## Project Structure

```
.
├── src/                    # Source files
│   └── main.cpp           # Main application
├── assets/                 # Asset files (icons, resources)
├── cmake/                  # CMake modules (if needed)
├── build/                  # Build output directory
│   ├── linux/             # Linux build
│   ├── windows/           # Windows build
│   └── macos/             # macOS build
├── .vscode/               # VS Code configuration
│   ├── settings.json      # Project settings
│   ├── tasks.json         # Build tasks
│   └── launch.json        # Debug configurations
├── CMakeLists.txt         # Main CMake configuration
├── windows-toolchain.cmake # Windows cross-compilation toolchain
├── build-*.sh             # Build scripts
├── package-*.sh           # Packaging scripts
└── README.md              # This file
```

## Troubleshooting

### Common Issues

1. **MinGW not found**:
   ```bash
   sudo apt install mingw-w64
   ```

2. **NSIS not found** (for Windows installer):
   ```bash
   sudo apt install nsis
   ```

3. **CMake version too old**:
   ```bash
   # Ubuntu/Debian
   sudo apt remove cmake
   sudo snap install cmake --classic
   ```

4. **Build fails on macOS**:
   - Ensure Xcode Command Line Tools are installed
   - Check CMake version (3.11+ required)

5. **Graphics issues in WSL**:
   - Install WSLg for GUI support
   - Or use X11 forwarding with VcXsrv/Xming

