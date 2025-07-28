#include "lua/LuaManager.h"
#include "lua/LuaHelpers.h"
#include "ResourceManager.h"
#include <iostream>
#include <cstring>

LuaManager& LuaManager::getInstance() {
    static LuaManager instance;
    return instance;
}

LuaManager::~LuaManager() {
    shutdown();
}

void LuaManager::initialize() {
    if (initialized) {
        return;
    }
    
    // Create main Lua state
    mainState = std::make_unique<LuaState>();
    mainState->registerGlobals();
    
    // Register default events
    registerEventType("on_init");
    registerEventType("on_tick");
    registerEventType("on_gui_click");
    registerEventType("on_entity_created");
    registerEventType("on_entity_damaged");
    registerEventType("on_entity_destroyed");
    
    // Update defines.events with registered events
    updateDefinesEvents();
    
    initialized = true;
    std::cout << "LuaManager initialized" << std::endl;
}

void LuaManager::shutdown() {
    if (!initialized) {
        return;
    }
    
    // Clean up Lua references
    if (mainState) {
        lua_State* L = mainState->get();
        
        // Unref init callback
        if (initCallbackRef != LUA_NOREF) {
            luaL_unref(L, LUA_REGISTRYINDEX, initCallbackRef);
            initCallbackRef = LUA_NOREF;
        }
        
        // Unref all event handlers
        for (auto& [event, handlers] : eventHandlers) {
            for (int ref : handlers) {
                luaL_unref(L, LUA_REGISTRYINDEX, ref);
            }
        }
        eventHandlers.clear();
    }
    
    // Clear state
    mainState.reset();
    initialized = false;
}

bool LuaManager::loadScript(const std::string& entityId) {
    if (!initialized) {
        std::cerr << "LuaManager not initialized" << std::endl;
        return false;
    }
    
    // Use ResourceManager to load the script
    auto& rm = ResourceManager::getInstance();
    std::string scriptContent = rm.loadScript(entityId);
    
    if (scriptContent.empty()) {
        std::cerr << "Failed to load script: " << entityId << std::endl;
        return false;
    }
    
    // Load from string instead of file
    bool success = loadScriptFromString(scriptContent, entityId);
    
    if (!success) {
        std::cerr << "Failed to load script: " << entityId << std::endl;
        return false;
    }
    
    // The script returns a value on the stack - pop it
    lua_pop(mainState->get(), 1);
    
    std::cout << "Loaded script: " << entityId << std::endl;
    return true;
}

bool LuaManager::executeString(const std::string& code, const std::string& name) {
    if (!initialized) {
        return false;
    }
    
    lua_State* L = mainState->get();
    
    if (luaL_loadbuffer(L, code.c_str(), code.length(), name.c_str()) != LUA_OK) {
        std::cerr << "Failed to load code: " << lua_tostring(L, -1) << std::endl;
        lua_pop(L, 1);
        return false;
    }
    
    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
        std::cerr << "Failed to execute code: " << lua_tostring(L, -1) << std::endl;
        lua_pop(L, 1);
        return false;
    }
    
    return true;
}

void LuaManager::registerEventType(const std::string& name) {
    if (eventNameToId.find(name) != eventNameToId.end()) {
        return; // Already registered
    }
    
    int id = static_cast<int>(registeredEvents.size() + 1);
    registeredEvents.push_back({name, id});
    eventNameToId[name] = id;
}

void LuaManager::queueEvent(const std::string& eventName, std::weak_ptr<void> target, 
                           const std::unordered_map<std::string, std::any>& data) {
    std::lock_guard<std::mutex> lock(eventQueueMutex);
    eventQueue.push({target, eventName, data});
}

void LuaManager::processEventQueue() {
    std::queue<QueuedEvent> localQueue;
    
    // Swap queues to minimize lock time
    {
        std::lock_guard<std::mutex> lock(eventQueueMutex);
        std::swap(localQueue, eventQueue);
    }
    
    // Process events
    while (!localQueue.empty()) {
        processEvent(localQueue.front());
        localQueue.pop();
    }
}

void LuaManager::processEvent(const QueuedEvent& event) {
    // Check if target still exists
    if (event.target.expired()) {
        return;
    }
    
    auto handlers = eventHandlers.find(event.eventName);
    if (handlers == eventHandlers.end()) {
        return; // No handlers
    }
    
    lua_State* L = mainState->get();
    
    for (int ref : handlers->second) {
        // Get handler function
        lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
        
        // Create event table
        lua_newtable(L);
        
        // Add event data
        // TODO: Convert std::any values to Lua
        
        // Call handler
        if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
            std::cerr << "Error in event handler: " << lua_tostring(L, -1) << std::endl;
            lua_pop(L, 1);
        }
    }
}

void LuaManager::updateDefinesEvents() {
    lua_State* L = mainState->get();
    
    // Get defines.events table
    lua_getglobal(L, "defines");
    lua_getfield(L, -1, "events");
    
    // Clear existing entries
    lua_pushnil(L);
    while (lua_next(L, -2) != 0) {
        lua_pop(L, 1);
        lua_pushvalue(L, -1);
        lua_pushnil(L);
        lua_settable(L, -4);
    }
    
    // Add all registered events
    for (const auto& event : registeredEvents) {
        lua_pushinteger(L, event.id);
        lua_setfield(L, -2, event.name.c_str());
    }
    
    lua_pop(L, 2); // Pop events and defines tables
}

int LuaManager::getEventId(const std::string& name) const {
    auto it = eventNameToId.find(name);
    return (it != eventNameToId.end()) ? it->second : -1;
}

// Script global implementations
int LuaManager::script_on_init(lua_State* L) {
    // Called as script:on_init(callback)
    // When using colon notation, Lua passes self as first argument
    // So we actually get 2 args: self and the callback
    if (lua_gettop(L) == 2) {
        // Colon notation: script:on_init(callback)
        // Remove self (first arg)
        lua_remove(L, 1);
    }
    
    LUA_CHECK_ARGS(1);
    LUA_CHECK_FUNCTION(1);
    
    // Get script object from upvalue
    lua_pushvalue(L, lua_upvalueindex(1));
    
    // Get ScriptContext from script object
    lua_getfield(L, -1, "__context");
    ScriptContext** pContext = (ScriptContext**)lua_touserdata(L, -1);
    lua_pop(L, 2); // Pop context and script object
    
    if (!pContext || !*pContext) {
        return luaL_error(L, "Invalid script context");
    }
    
    ScriptContext* context = *pContext;
    
    // Store callback in registry and save reference in context
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    context->setInitCallback(ref);
    
    // Call the callback immediately
    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
        std::cerr << "Error in on_init for " << context->getId() << ": " 
                  << lua_tostring(L, -1) << std::endl;
        lua_pop(L, 1);
    }
    
    return 0;
}

int LuaManager::script_on_event(lua_State* L) {
    // Called as script:on_event(event_id, callback)
    // When using colon notation, Lua passes self as first argument
    int argCount = lua_gettop(L);
    if (argCount == 3) {
        // Colon notation: script:on_event(event_id, callback)
        // Remove self (first arg)
        lua_remove(L, 1);
    }
    
    LUA_CHECK_ARGS(2);
    LUA_CHECK_NUMBER(1);
    LUA_CHECK_FUNCTION(2);
    
    int eventId = lua_tointeger(L, 1);
    
    // Get script object from upvalue
    lua_pushvalue(L, lua_upvalueindex(1));
    
    // Get ScriptContext from script object
    lua_getfield(L, -1, "__context");
    ScriptContext** pContext = (ScriptContext**)lua_touserdata(L, -1);
    lua_pop(L, 2); // Pop context and script object
    
    if (!pContext || !*pContext) {
        return luaL_error(L, "Invalid script context");
    }
    
    ScriptContext* context = *pContext;
    
    // Find event name by ID
    auto& manager = getInstance();
    std::string eventName;
    for (const auto& event : manager.registeredEvents) {
        if (event.id == eventId) {
            eventName = event.name;
            break;
        }
    }
    
    if (eventName.empty()) {
        return luaL_error(L, "Invalid event ID: %d", eventId);
    }
    
    // Store callback in registry and save reference in context
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    context->addEventHandler(eventName, ref);
    
    return 0;
}

// Game global implementations
int LuaManager::game_print(lua_State* L) {
    LUA_CHECK_MIN_ARGS(1);
    
    const char* msg = lua_tostring(L, 1);
    if (msg) {
        std::cout << "[Lua] " << msg << std::endl;
    }
    
    return 0;
}

int LuaManager::game_get_pawn(lua_State* L) {
    LUA_CHECK_ARGS(1);
    LUA_CHECK_NUMBER(1);
    
    int pawnId = lua_tointeger(L, 1);
    
    // Create pawn table
    lua_newtable(L);
    
    // Add pawn id
    lua_pushinteger(L, pawnId);
    lua_setfield(L, -2, "id");
    
    // Create gui table
    lua_newtable(L);
    
    // Add add_frame method to gui
    lua_pushcfunction(L, gui_add_frame);
    lua_setfield(L, -2, "add_frame");
    
    // Set gui on pawn
    lua_setfield(L, -2, "gui");
    
    return 1;
}

int LuaManager::game_exit(lua_State* L) {
    // TODO: Trigger application exit
    std::cout << "[Lua] game.exit() called" << std::endl;
    return 0;
}

// Data global implementations
int LuaManager::data_extend(lua_State* L) {
    // When called as data:extend(), first arg is self (data table), second is the array
    int argCount = lua_gettop(L);
    int tableArg = (argCount == 2) ? 2 : 1; // Handle both data:extend() and data.extend()
    
    if (!lua_istable(L, tableArg)) {
        return luaL_error(L, "data:extend() expects a table argument");
    }
    
    // TODO: Process prototype definitions
    std::cout << "[Lua] data:extend() called" << std::endl;
    
    return 0;
}

// Custom require implementation
int LuaManager::lua_require(lua_State* L) {
    LUA_CHECK_ARGS(1);
    const char* modname = lua_tostring(L, 1);
    if (!modname) {
        return luaL_error(L, "module name must be a string");
    }
    
    // Check package.loaded first
    lua_getglobal(L, "package");
    if (!lua_isnil(L, -1)) {
        lua_getfield(L, -1, "loaded");
        lua_getfield(L, -1, modname);
        if (!lua_isnil(L, -1)) {
            // Module already loaded, return it
            lua_remove(L, -2); // Remove loaded table
            lua_remove(L, -2); // Remove package table
            return 1;
        }
        lua_pop(L, 3); // Pop nil, loaded, package
    } else {
        lua_pop(L, 1); // Pop nil package
    }
    
    // Use ResourceManager to load the module script
    auto& rm = ResourceManager::getInstance();
    auto& manager = getInstance();
    
    // First try the module name as-is (could be a full entity ID)
    std::string scriptContent = rm.loadScript(modname);
    
    // If not found, try with default org.mod prefix
    if (scriptContent.empty()) {
        std::string entityId = std::string("dorgby.dco.") + modname;
        scriptContent = rm.loadScript(entityId);
    }
    
    if (scriptContent.empty()) {
        return luaL_error(L, "module '%s' not found", modname);
    }
    
    // Load the module script
    bool success = manager.loadScriptFromString(scriptContent, modname);
    
    if (!success) {
        return luaL_error(L, "failed to load module '%s'", modname);
    }
    
    // The module return value is on the stack
    // Store in package.loaded
    lua_getglobal(L, "package");
    if (!lua_isnil(L, -1)) {
        lua_getfield(L, -1, "loaded");
        lua_pushvalue(L, -3); // Push module result
        lua_setfield(L, -2, modname);
        lua_pop(L, 2); // Pop loaded and package
    } else {
        lua_pop(L, 1); // Pop nil package
    }
    
    // Return the module (already on stack from loadScriptFile)
    return 1;
}

// GUI implementations
int LuaManager::gui_add_frame(lua_State* L) {
    LUA_CHECK_ARGS(1);
    LUA_CHECK_TABLE(1);
    
    // Get frame config
    lua_getfield(L, 1, "name");
    const char* name = lua_tostring(L, -1);
    lua_pop(L, 1);
    
    lua_getfield(L, 1, "caption");
    const char* caption = lua_tostring(L, -1);
    lua_pop(L, 1);
    
    std::cout << "[Lua] Creating frame '" << (name ? name : "unnamed") 
              << "' with caption: " << (caption ? caption : "") << std::endl;
    
    // Create frame table
    lua_newtable(L);
    
    // Store frame properties
    lua_pushvalue(L, 1); // Copy config table
    lua_setfield(L, -2, "config");
    
    // Add frame methods
    lua_pushcfunction(L, frame_on_init);
    lua_setfield(L, -2, "on_init");
    
    lua_pushcfunction(L, frame_add_button);
    lua_setfield(L, -2, "add_button");
    
    // Set name
    if (name) {
        lua_pushstring(L, name);
        lua_setfield(L, -2, "name");
    }
    
    return 1;
}

int LuaManager::frame_on_init(lua_State* L) {
    // Called as frame:on_init(callback) - self is first arg
    int argCount = lua_gettop(L);
    if (argCount < 2 || !lua_istable(L, 1) || !lua_isfunction(L, 2)) {
        return luaL_error(L, "frame:on_init() expects a function argument");
    }
    
    // Create event table
    lua_newtable(L);
    
    // Set element to self (the frame - first argument)
    lua_pushvalue(L, 1);
    lua_setfield(L, -2, "element");
    
    // Call the callback immediately with the event
    lua_pushvalue(L, 2); // Push callback
    lua_pushvalue(L, -2); // Push event
    
    if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
        std::cerr << "Error in frame on_init: " << lua_tostring(L, -1) << std::endl;
        lua_pop(L, 1);
    }
    
    return 0;
}

int LuaManager::frame_add_button(lua_State* L) {
    // Called as frame:add_button(config) - self is first arg
    int argCount = lua_gettop(L);
    if (argCount < 2 || !lua_istable(L, 1) || !lua_istable(L, 2)) {
        return luaL_error(L, "frame:add_button() expects a table argument");
    }
    
    // Get button config from second argument
    lua_getfield(L, 2, "name");
    const char* name = lua_tostring(L, -1);
    lua_pop(L, 1);
    
    lua_getfield(L, 2, "caption");
    const char* caption = lua_tostring(L, -1);
    lua_pop(L, 1);
    
    std::cout << "[Lua] Creating button '" << (name ? name : "unnamed") 
              << "' with caption: " << (caption ? caption : "") << std::endl;
    
    // Create button table
    lua_newtable(L);
    
    // Store button properties
    lua_pushvalue(L, 2); // Copy config table (second argument)
    lua_setfield(L, -2, "config");
    
    // Add button methods
    lua_pushcfunction(L, button_on_init);
    lua_setfield(L, -2, "on_init");
    
    lua_pushcfunction(L, button_on_click);
    lua_setfield(L, -2, "on_click");
    
    lua_pushcfunction(L, button_on_input);
    lua_setfield(L, -2, "on_input");
    
    // Set properties
    if (name) {
        lua_pushstring(L, name);
        lua_setfield(L, -2, "name");
    }
    if (caption) {
        lua_pushstring(L, caption);
        lua_setfield(L, -2, "caption");
    }
    
    return 1;
}

int LuaManager::button_on_init(lua_State* L) {
    // Called as button:on_init(callback) - self is first arg
    int argCount = lua_gettop(L);
    if (argCount < 2 || !lua_istable(L, 1) || !lua_isfunction(L, 2)) {
        return luaL_error(L, "button:on_init() expects a function argument");
    }
    
    // Create event table
    lua_newtable(L);
    
    // Set element to self (the button - first argument)
    lua_pushvalue(L, 1);
    lua_setfield(L, -2, "element");
    
    // Call the callback immediately with the event
    lua_pushvalue(L, 2); // Push callback
    lua_pushvalue(L, -2); // Push event
    
    if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
        std::cerr << "Error in button on_init: " << lua_tostring(L, -1) << std::endl;
        lua_pop(L, 1);
    }
    
    return 0;
}

int LuaManager::button_on_click(lua_State* L) {
    // Called as button:on_click(callback) - self is first arg
    int argCount = lua_gettop(L);
    if (argCount < 2 || !lua_istable(L, 1) || !lua_isfunction(L, 2)) {
        return luaL_error(L, "button:on_click() expects a function argument");
    }
    
    // Store the callback for later use
    // For now, just log that we registered a click handler
    std::cout << "[Lua] Registered click handler for button" << std::endl;
    
    return 0;
}

int LuaManager::button_on_input(lua_State* L) {
    // Called as button:on_input(event_name, callback) - self is first arg
    int argCount = lua_gettop(L);
    if (argCount < 3 || !lua_istable(L, 1) || !lua_isstring(L, 2) || !lua_isfunction(L, 3)) {
        return luaL_error(L, "button:on_input() expects event name and function");
    }
    
    const char* eventName = lua_tostring(L, 2);
    
    // Store the callback for later use
    // For now, just log that we registered an input handler
    std::cout << "[Lua] Registered input handler for event '" << eventName << "'" << std::endl;
    
    return 0;
}

bool LuaManager::loadScriptFile(const std::string& filepath, const std::string& chunkname) {
    lua_State* L = mainState->get();
    
    // Create the ScriptContext
    auto context = std::make_unique<ScriptContext>(chunkname, filepath);
    
    // Load the script file
    if (!context->load(L)) {
        return false;
    }
    
    // Create a script object for this script
    lua_newtable(L); // script object
    
    // Store ScriptContext pointer as userdata
    ScriptContext** pContext = (ScriptContext**)lua_newuserdata(L, sizeof(ScriptContext*));
    *pContext = context.get();
    lua_setfield(L, -2, "__context");
    
    // Add script methods with context as upvalue
    lua_pushvalue(L, -1); // Push script object as upvalue
    lua_pushcclosure(L, script_on_init, 1);
    lua_setfield(L, -2, "on_init");
    
    lua_pushvalue(L, -1); // Push script object as upvalue
    lua_pushcclosure(L, script_on_event, 1);
    lua_setfield(L, -2, "on_event");
    
    // Create environment for the script
    lua_newtable(L); // New environment
    
    // Set script object in environment
    lua_pushvalue(L, -2); // Copy script object
    lua_setfield(L, -2, "script");
    
    // Copy allowed globals to environment
    const char* allowed[] = {
        "pairs", "ipairs", "type", "tostring", "tonumber",
        "assert", "error", "pcall", "xpcall", "select", "next",
        "print", "math", "string", "table", "require", "package",
        "game", "defines", "data", "translate", "_G", nullptr
    };
    
    for (const char** name = allowed; *name; ++name) {
        lua_getglobal(L, *name);
        lua_setfield(L, -2, *name);
    }
    
    // Set _ENV for the loaded chunk (which is below script object)
    lua_setupvalue(L, -3, 1);
    
    // Stack now has: [function] [script object]
    // We need to swap them so function is on top
    lua_insert(L, -2);
    
    // Execute the script with its environment
    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
        std::cerr << "Error executing " << chunkname << ": " << lua_tostring(L, -1) << std::endl;
        lua_pop(L, 2); // Pop error and script object
        return false;
    }
    
    // Store the context
    m_loadedScripts[chunkname] = std::move(context);
    
    // Pop the script object
    lua_pop(L, 1);
    
    // Push a dummy return value for consistency with require()
    lua_pushboolean(L, 1);
    
    return true;
}

bool LuaManager::loadScriptFromString(const std::string& content, const std::string& chunkname) {
    lua_State* L = mainState->get();
    
    // Create the ScriptContext
    auto context = std::make_unique<ScriptContext>(chunkname, chunkname);
    
    // Load the script content
    if (luaL_loadbuffer(L, content.c_str(), content.length(), chunkname.c_str()) != LUA_OK) {
        std::cerr << "Error loading script " << chunkname << ": " << lua_tostring(L, -1) << std::endl;
        lua_pop(L, 1);
        return false;
    }
    
    // Create a script object for this script
    lua_newtable(L); // script object
    
    // Store ScriptContext pointer as userdata
    ScriptContext** pContext = (ScriptContext**)lua_newuserdata(L, sizeof(ScriptContext*));
    *pContext = context.get();
    lua_setfield(L, -2, "__context");
    
    // Add script methods with context as upvalue
    lua_pushvalue(L, -1); // Push script object as upvalue
    lua_pushcclosure(L, script_on_init, 1);
    lua_setfield(L, -2, "on_init");
    
    lua_pushvalue(L, -1); // Push script object as upvalue
    lua_pushcclosure(L, script_on_event, 1);
    lua_setfield(L, -2, "on_event");
    
    // Create environment for the script
    lua_newtable(L); // New environment
    
    // Set script object in environment
    lua_pushvalue(L, -2); // Copy script object
    lua_setfield(L, -2, "script");
    
    // Copy allowed globals to environment
    const char* allowed[] = {
        "pairs", "ipairs", "type", "tostring", "tonumber",
        "assert", "error", "pcall", "xpcall", "select", "next",
        "print", "math", "string", "table", "require", "package",
        "game", "defines", "data", "translate", "_G", nullptr
    };
    
    for (const char** name = allowed; *name; ++name) {
        lua_getglobal(L, *name);
        lua_setfield(L, -2, *name);
    }
    
    // Set _ENV for the loaded chunk (which is below script object)
    lua_setupvalue(L, -3, 1);
    
    // Stack now has: [function] [script object]
    // We need to swap them so function is on top
    lua_insert(L, -2);
    
    // Execute the script with its environment
    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
        std::cerr << "Error executing " << chunkname << ": " << lua_tostring(L, -1) << std::endl;
        lua_pop(L, 2); // Pop error and script object
        return false;
    }
    
    // Store the context
    m_loadedScripts[chunkname] = std::move(context);
    
    // Pop the script object
    lua_pop(L, 1);
    
    // Push a dummy return value for consistency with require()
    lua_pushboolean(L, 1);
    
    return true;
}