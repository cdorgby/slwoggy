#include "ApplicationView.h"
#include "ResourceManager.h"
#include "InputManager.h"
#include <iostream>
#include <algorithm>

ApplicationView& ApplicationView::getInstance() {
    static ApplicationView instance;
    return instance;
}

void ApplicationView::initialize() {
    if (m_initialized) {
        return;
    }
    
    m_initialized = true;
    std::cout << "ApplicationView initialized" << std::endl;
}

void ApplicationView::shutdown() {
    // Unregister all entities
    
    m_initialized = false;
}

void ApplicationView::update(float deltaTime) {
    if (!m_initialized) return;
}

void ApplicationView::render() {
    if (!m_initialized) return;
    
    BeginDrawing();
    ClearBackground(DARKGRAY);  // Data center background
    
    EndDrawing();
}

void ApplicationView::processInput() {
    if (!m_initialized) return;
    
    auto& input = InputManager::getInstance();
    
    // Global hotkeys
    if (input.isActionPressed("navigate_back")) {
        // ESC pressed - could show main menu or quit
        // For now, just quit
        requestQuit();
    }
    
    // Get mouse state
    Vector2 mousePos = GetMousePosition();
    bool mousePressed = IsMouseButtonPressed(MOUSE_LEFT_BUTTON);
    
}

ApplicationView::~ApplicationView() {
    shutdown();
}