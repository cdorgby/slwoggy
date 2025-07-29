#include "log.h"
CLOG_MODULE("main")

#include "raylib.h"
#include "InputManager.h"
#include "ResourceManager.h"
#include "lua/LuaManager.h"

#include <string>

int main(int argc, char *argv[])
{
    c_log_init("RaylibHelloWorld", C_LOG_GENERIC, C_LOG_LEVEL_DEBUG3);

    ILOG("Starting Data Center Operations...");

    // Initialization
    const int screenWidth  = 800;
    const int screenHeight = 600;

    InitWindow(screenWidth, screenHeight, "Data Center Operations");
    
    // Disable raylib's default ESC to close behavior
    SetExitKey(0);
    
    // Initialize resource manager with executable path
    std::string exePath = (argc > 0) ? argv[0] : "RaylibHelloWorld";
    ResourceManager::getInstance().initialize(exePath);
    
    // Set up multiple module paths for resource loading
    // This builds the entity map from multiple installation roots
    // For example:
    // - assets/ (local development)
    // - /usr/share/dco/modules/ (system-wide Linux)
    // - C:\Program Files\DCO\modules\ (Windows)
    // Each module has its own catalog.json defining org.mod.entity mappings
    
    // Get input manager
    InputManager &inputManager = InputManager::getInstance();
    
    // Initialize Lua
    LuaManager::getInstance().initialize();
    
    // Load init script
    LuaManager::getInstance().loadScript("dorgby.dco.init");
    
    SetTargetFPS(60);

    // Main game loop
    while (!WindowShouldClose())
    {
        // Update input manager
        inputManager.update();
        
        // Check for ESC to quit
        if (inputManager.isActionPressed("navigate_back"))
        {
            break;
        }

        // Render
        BeginDrawing();
        ClearBackground(BLACK);
        
        // Draw some random text
        DrawText("Data Center Operations", 190, 200, 32, LIGHTGRAY);
        DrawText("Press ESC to exit", 280, 300, 20, GRAY);
        
        EndDrawing();
        
        // Process Lua events at end of frame
        LuaManager::getInstance().processEventQueue();
    }

    // De-Initialization
    LuaManager::getInstance().shutdown();
    CloseWindow();

    return 0;
}