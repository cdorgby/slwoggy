#ifndef INPUT_MANAGER_H
#define INPUT_MANAGER_H

#include "raylib.h"
#include <unordered_map>
#include <string>

// Input binding configuration
struct KeyBinding {
    KeyboardKey key;
    bool requireCtrl = false;
    bool requireShift = false;
    bool requireAlt = false;
    
    bool matches(bool ctrl, bool shift, bool alt) const {
        return (requireCtrl == ctrl) && 
               (requireShift == shift) && 
               (requireAlt == alt);
    }
};

// Action state for a single frame
struct ActionState {
    bool pressed = false;   // Just pressed this frame
    bool held = false;      // Currently held down
    bool released = false;  // Just released this frame
};

class InputManager {
public:
    static InputManager& getInstance();
    
    // Update input states - call once per frame before checking states
    void update();
    
    // Query action states
    bool isActionPressed(const std::string& action) const;
    bool isActionHeld(const std::string& action) const;
    bool isActionReleased(const std::string& action) const;
    ActionState getActionState(const std::string& action) const;
    
    // Configure key bindings
    void bindKey(const std::string& action, KeyBinding binding);
    void unbindKey(const std::string& action);
    
    // Mouse state
    Vector2 getMousePosition() const { return GetMousePosition(); }
    Vector2 getMouseDelta() const { return GetMouseDelta(); }
    bool isMouseButtonPressed(int button) const { return IsMouseButtonPressed(button); }
    bool isMouseButtonDown(int button) const { return IsMouseButtonDown(button); }
    bool isMouseButtonReleased(int button) const { return IsMouseButtonReleased(button); }
    
    // Direct key state (for special cases)
    bool isKeyDown(int key) const { return IsKeyDown(key); }
    bool isKeyPressed(int key) const { return IsKeyPressed(key); }
    bool isKeyReleased(int key) const { return IsKeyReleased(key); }
    
private:
    InputManager();
    ~InputManager() = default;
    
    InputManager(const InputManager&) = delete;
    InputManager& operator=(const InputManager&) = delete;
    
    // Key bindings
    std::unordered_map<std::string, KeyBinding> keyBindings;
    
    // Action states for current frame
    std::unordered_map<std::string, ActionState> actionStates;
    
    // Previous frame's held state for edge detection
    std::unordered_map<std::string, bool> previousHeldState;
    
    // Update action states based on key bindings
    void updateActionStates();
    
    // Default key bindings
    void setupDefaultBindings();
};

#endif // INPUT_MANAGER_H