#ifndef LUA_MENU_SCENE_H
#define LUA_MENU_SCENE_H

#include "Scene.h"
#include <vector>
#include <memory>

// Forward declarations
struct lua_State;
class LuaUIEntity;

class LuaMenuScene : public Scene {
public:
    LuaMenuScene();
    ~LuaMenuScene();
    
    void onEnter() override;
    void onExit() override;
    void update(float deltaTime) override;
    void render() override;
    void processInput() override;
    
private:
    lua_State* L = nullptr;
    std::vector<LuaUIEntity*> uiElements;
    bool luaInitialized = false;
    
    // Initialize Lua environment for this scene
    bool initializeLua();
    void cleanupLua();
    
    // Load and execute the menu script
    bool loadMenuScript();
};

#endif // LUA_MENU_SCENE_H