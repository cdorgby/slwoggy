#ifndef APPLICATION_VIEW_H
#define APPLICATION_VIEW_H

#include <vector>
#include <memory>
#include <string>
#include "raylib.h"

// Forward declarations

class ApplicationView {
public:
    static ApplicationView& getInstance();
    
    // Initialize the application view
    void initialize();
    
    // Shutdown and cleanup
    void shutdown();
    
    // Main update/render loop
    void update(float deltaTime);
    void render();
    void processInput();
    
    // Request application quit
    void requestQuit() { m_shouldQuit = true; }
    bool shouldQuit() const { return m_shouldQuit; }
    
private:
    ApplicationView() = default;
    ~ApplicationView();
    
    ApplicationView(const ApplicationView&) = delete;
    ApplicationView& operator=(const ApplicationView&) = delete;
    
    // Application state
    bool m_initialized = false;
    bool m_shouldQuit = false;
};

#endif // APPLICATION_VIEW_H