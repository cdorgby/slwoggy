#include "InputManager.h"
#include <iostream>

InputManager &InputManager::getInstance()
{
    static InputManager instance;
    return instance;
}

InputManager::InputManager() { setupDefaultBindings(); }

void InputManager::setupDefaultBindings()
{
    // Navigation
    bindKey("navigate_back", {KEY_ESCAPE});
    bindKey("navigate_home", {KEY_HOME});
    bindKey("navigate_home_alt", {KEY_H, true, false, false}); // Ctrl+H

    // UI Actions
    bindKey("confirm", {KEY_ENTER});
    bindKey("confirm_alt", {KEY_SPACE});
    bindKey("cancel", {KEY_ESCAPE}); // Can overlap with navigate_back

    // Application
    bindKey("quit", {KEY_Q, true, false, false}); // Ctrl+Q

    // Menu navigation
    bindKey("menu_up", {KEY_UP});
    bindKey("menu_down", {KEY_DOWN});
    bindKey("menu_left", {KEY_LEFT});
    bindKey("menu_right", {KEY_RIGHT});

    // Numbers for menu selection
    bindKey("select_1", {KEY_ONE});
    bindKey("select_2", {KEY_TWO});
    bindKey("select_3", {KEY_THREE});
    bindKey("select_4", {KEY_FOUR});
    bindKey("select_5", {KEY_FIVE});
}

void InputManager::update()
{
    // Save previous held states for edge detection
    previousHeldState.clear();
    for (const auto& [action, state] : actionStates) {
        previousHeldState[action] = state.held;
    }
    
    // Clear current frame states
    actionStates.clear();
    
    // Update action states based on key bindings
    updateActionStates();
}

void InputManager::updateActionStates()
{
    bool ctrl  = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    bool alt   = IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);

    for (const auto& [action, binding] : keyBindings)
    {
        bool keyHeld = IsKeyDown(binding.key) && binding.matches(ctrl, shift, alt);
        bool wasHeld = previousHeldState.find(action) != previousHeldState.end() && previousHeldState[action];
        
        ActionState& state = actionStates[action];
        state.held = keyHeld;
        state.pressed = keyHeld && !wasHeld;
        state.released = !keyHeld && wasHeld;
    }
}

bool InputManager::isActionPressed(const std::string& action) const
{
    auto it = actionStates.find(action);
    return it != actionStates.end() && it->second.pressed;
}

bool InputManager::isActionHeld(const std::string& action) const
{
    auto it = actionStates.find(action);
    return it != actionStates.end() && it->second.held;
}

bool InputManager::isActionReleased(const std::string& action) const
{
    auto it = actionStates.find(action);
    return it != actionStates.end() && it->second.released;
}

ActionState InputManager::getActionState(const std::string& action) const
{
    auto it = actionStates.find(action);
    if (it != actionStates.end()) {
        return it->second;
    }
    return ActionState{};
}

void InputManager::bindKey(const std::string& action, KeyBinding binding) 
{ 
    keyBindings[action] = binding; 
}

void InputManager::unbindKey(const std::string& action) 
{ 
    keyBindings.erase(action); 
}