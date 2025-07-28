#pragma once

#include "lua/LuaState.h"
#include "lua/ScriptContext.h"
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <queue>
#include <mutex>
#include <any>

// Forward declarations
struct lua_State;

// Event definition for defines.events
struct EventDefinition {
    std::string name;
    int id;
};

// Queued event for batched processing
struct QueuedEvent {
    std::weak_ptr<void> target;  // Weak ref to entity/element
    std::string eventName;
    std::unordered_map<std::string, std::any> data;
};

class LuaManager {
public:
    static LuaManager& getInstance();
    
    // Lifecycle
    void initialize();
    void shutdown();
    bool isInitialized() const { return initialized; }
    
    // Script loading
    bool loadScript(const std::string& entityId);
    bool executeString(const std::string& code, const std::string& name = "=string");
    
    // Event management
    void registerEventType(const std::string& name);
    void queueEvent(const std::string& eventName, std::weak_ptr<void> target, 
                    const std::unordered_map<std::string, std::any>& data);
    void processEventQueue();
    
    // Get event ID by name
    int getEventId(const std::string& name) const;
    
    // Script global callbacks (called from Lua)
    static int script_on_init(lua_State* L);
    static int script_on_event(lua_State* L);
    
    // Game global callbacks
    static int game_print(lua_State* L);
    static int game_get_pawn(lua_State* L);
    static int game_exit(lua_State* L);
    
    // Data global callbacks
    static int data_extend(lua_State* L);
    
    // Custom require implementation
    static int lua_require(lua_State* L);
    
    // GUI callbacks
    static int gui_add_frame(lua_State* L);
    static int frame_on_init(lua_State* L);
    static int frame_add_button(lua_State* L);
    static int button_on_init(lua_State* L);
    static int button_on_click(lua_State* L);
    static int button_on_input(lua_State* L);
    
private:
    LuaManager() = default;
    ~LuaManager();
    
    LuaManager(const LuaManager&) = delete;
    LuaManager& operator=(const LuaManager&) = delete;
    
    // Process a single queued event
    void processEvent(const QueuedEvent& event);
    
    // Build defines.events table from registered events
    void updateDefinesEvents();
    
    // Common script loading implementation
    bool loadScriptFile(const std::string& filepath, const std::string& chunkname);
    bool loadScriptFromString(const std::string& content, const std::string& chunkname);
    
    // Member data
    std::unique_ptr<LuaState> mainState;
    bool initialized = false;
    
    // Event system
    std::vector<EventDefinition> registeredEvents;
    std::unordered_map<std::string, int> eventNameToId;
    std::unordered_map<std::string, std::vector<int>> eventHandlers; // event -> [lua refs]
    
    // Event queue (thread-safe)
    std::queue<QueuedEvent> eventQueue;
    std::mutex eventQueueMutex;
    
    // Script init callback
    int initCallbackRef = LUA_NOREF;
    
    // Loaded scripts - maps script ID to context
    std::unordered_map<std::string, std::unique_ptr<ScriptContext>> m_loadedScripts;
};