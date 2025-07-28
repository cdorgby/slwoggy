#include "scenes/LuaMenuScene.h"
#include "lua/LuaManager.h"
#include "ui/LuaUIEntity.h"
#include "ui/LuaButton.h"
#include "ResourceManager.h"
#include "InputManager.h"
#include "SceneManager.h"
#include "raylib.h"
#include <iostream>

LuaMenuScene::LuaMenuScene() : Scene("LuaMainMenu") {
}

LuaMenuScene::~LuaMenuScene() {
    cleanupLua();
}

void LuaMenuScene::onEnter() {
    Scene::onEnter();
    
    if (!luaInitialized) {
        if (!initializeLua()) {
            std::cerr << "Failed to initialize Lua for menu scene" << std::endl;
            return;
        }
        
        if (!loadMenuScript()) {
            std::cerr << "Failed to load menu script" << std::endl;
            return;
        }
        
        luaInitialized = true;
    }
}

void LuaMenuScene::onExit() {
    Scene::onExit();
    // Keep Lua state alive between visits to the scene
}

bool LuaMenuScene::initializeLua() {
    // Get the Lua manager and initialize if needed
    auto& luaManager = LuaManager::getInstance();
    if (!luaManager.initialize()) {
        return false;
    }
    
    L = luaManager.getState();
    return L != nullptr;
}

void LuaMenuScene::cleanupLua() {
    // Clean up UI elements
    for (auto* element : uiElements) {
        if (element) {
            element->invalidate();
        }
    }
    uiElements.clear();
    
    // Note: We don't close the Lua state here as it's managed by LuaManager
}

bool LuaMenuScene::loadMenuScript() {
    if (!L) return false;
    
    // Create a scene context for Lua scripts
    lua_newtable(L);
    
    // Add function to register UI elements with the scene
    lua_pushcfunction(L, [](lua_State* L) -> int {
        // Get the UI element from the stack
        LuaUIEntity* element = checkObject<LuaUIEntity>(L, 1);
        
        // Get the scene instance (stored as upvalue)
        lua_getglobal(L, "_currentLuaMenuScene");
        LuaMenuScene* scene = static_cast<LuaMenuScene*>(lua_touserdata(L, -1));
        lua_pop(L, 1);
        
        if (scene && element) {
            scene->uiElements.push_back(element);
        }
        
        return 0;
    });
    lua_setfield(L, -2, "addElement");
    
    lua_setglobal(L, "Scene");
    
    // Store pointer to this scene for callbacks
    lua_pushlightuserdata(L, this);
    lua_setglobal(L, "_currentLuaMenuScene");
    
    // Load the menu script
    bool success = ResourceManager::getInstance().loadScript(L, "menu.lua");
    
    if (success) {
        // The script's return value is on the stack
        if (lua_istable(L, -1)) {
            // Store it as global "menu"
            lua_setglobal(L, "menu");
            
            // Get it back to call init
            lua_getglobal(L, "menu");
            lua_getfield(L, -1, "init");
            if (lua_isfunction(L, -1)) {
                lua_pushvalue(L, -2); // Push menu table as self
                if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
                    const char* error = lua_tostring(L, -1);
                    std::cerr << "Error calling menu:init(): " << error << std::endl;
                    lua_pop(L, 1);
                    success = false;
                }
            } else {
                lua_pop(L, 1); // Pop non-function
            }
            
            // Store menu table reference
            lua_setglobal(L, "_menuInstance");
        } else {
            lua_pop(L, 1); // Pop non-table
            std::cerr << "menu.lua did not return a table" << std::endl;
            success = false;
        }
    }
    
    // Clean up temporary global
    lua_pushnil(L);
    lua_setglobal(L, "_currentLuaMenuScene");
    
    return success;
}

void LuaMenuScene::update(float deltaTime) {
    // Update all UI elements
    for (auto* element : uiElements) {
        if (element && element->isValid()) {
            element->update(deltaTime);
        }
    }
}

void LuaMenuScene::render() {
    BeginDrawing();
    ClearBackground(RAYWHITE);
    
    // Title (hardcoded for now, could be moved to Lua)
    const char* title = "Raylib Hello World";
    int titleWidth = MeasureText(title, 40);
    DrawText(title, GetScreenWidth()/2 - titleWidth/2, 80, 40, DARKGRAY);
    
    // Render all UI elements
    for (auto* element : uiElements) {
        if (element && element->isValid()) {
            element->render();
        }
    }
    
    // Instructions
    DrawText("Click buttons or press ESC to go back", 
            GetScreenWidth()/2 - 150, GetScreenHeight() - 50, 16, DARKGRAY);
    
    EndDrawing();
}

void LuaMenuScene::processInput() {
    auto& input = InputManager::getInstance();
    
    // Handle back navigation
    if (input.isActionPressed("navigate_back")) {
        // Menu is root scene, so quit on back
        if (sceneManager) {
            sceneManager->requestQuit();
        }
    }
    
    // Get mouse state
    Vector2 mousePos = GetMousePosition();
    bool mousePressed = IsMouseButtonPressed(MOUSE_LEFT_BUTTON);
    
    // Process input for all UI elements
    for (auto* element : uiElements) {
        if (element && element->isValid()) {
            if (element->handleInput(mousePos, mousePressed)) {
                break; // Stop if an element handled the input
            }
        }
    }
}