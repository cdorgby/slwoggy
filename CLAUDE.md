# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```bash
# Build for Windows (from WSL/Linux)
./build-windows.sh

# Build for Linux
./build-linux.sh

# Build all platforms
./build-all.sh

# Run the Windows build from WSL
cd build/windows/bin && ./RaylibHelloWorld.exe
```

## Architecture Overview

This is a raylib-based application with a scene management system and event-driven input handling.

### Core Systems

**Scene System** (`include/Scene.h`, `src/SceneManager.cpp`)
- Base `Scene` class with lifecycle methods (`onEnter`, `onExit`, `onSuspend`, `onResume`)
- `SceneManager` maintains a scene stack for navigation
- Scenes can request transitions via `requestSceneChange()` or `requestPopScene()`
- Override `needsContinuousRendering()` to indicate if a scene requires constant redraws

**Event System** (`include/Event.h`, `src/InputManager.cpp`)
- Input is abstracted into semantic events (NAVIGATE_BACK, CONFIRM, etc.)
- `InputManager` converts raw raylib input to events
- Scenes handle events via `handleEvent()` method
- ESC key is reserved for NAVIGATE_BACK - do not bind to CANCEL

**Resource Management** (`src/ResourceManager.cpp`)
- Singleton that handles asset loading with platform-aware path detection
- Automatically finds assets whether running from build dir, installed location, or Steam

### Key Implementation Details

**Window Focus on Windows**
- When launching from WSL, the window won't get focus due to Windows security
- The taskbar icon will blink orange - user must click to give focus
- This is expected behavior and cannot be bypassed

**CPU Usage Optimization**
- Static scenes (like MenuScene) automatically reduce to 10 FPS when idle
- Animated scenes (shader demos) maintain 60 FPS via `needsContinuousRendering()`
- Do NOT use `EnableEventWaiting()` - it breaks smooth animations

**ESC Key Handling**
- `SetExitKey(0)` disables raylib's default ESC-to-close behavior
- ESC is used for scene navigation (going back)
- Only the root scene (MenuScene) exits the app on ESC

## Common Tasks

### Adding a New Scene
1. Create header in `include/scenes/` and implementation in `src/scenes/`
2. Inherit from `Scene` class
3. Override `update()`, `render()`, and `handleEvent()`
4. Add to CMakeLists.txt SOURCES list
5. Register in main.cpp with `sceneManager.registerScene()`

### Adding Shader Effects
1. Place shader files in `assets/shaders/`
2. Load via `ResourceManager::getInstance().loadShader()`
3. Override `needsContinuousRendering()` to return true for animated shaders

### Debugging Navigation Issues
- Check if ESC is bound to multiple actions in `InputManager::setupDefaultBindings()`
- Verify scene stack size with debug logging in `SceneManager`
- Ensure `SetExitKey(0)` is called after `InitWindow()`

## Platform Notes

**Windows Build from WSL**
- Uses MinGW cross-compiler
- Paths will be WSL-style (e.g., `//wsl$/Ubuntu-24.04/...`)
- Window will not get focus when launched from WSL terminal

**Resource Paths**
- Development: `<exe_dir>/../assets/`
- Installed Linux: `/usr/share/RaylibHelloWorld/assets/`
- Installed Windows: `<exe_dir>/../assets/`
- Steam: Various platform-specific locations

## Important Warnings

1. **Do NOT enable SUPPORT_CUSTOM_FRAME_CONTROL** - it requires modifying raylib's build
2. **Do NOT use platform-specific #ifdefs** for raylib functions - raylib handles this internally
3. **Vector2Length does not exist** - use `Vector2Distance({0,0}, vec)` or check x,y manually
4. **SetWindowFocused() exists** but doesn't guarantee focus on Windows due to security