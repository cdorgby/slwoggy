#include "lua/ScriptContext.h"
#include <iostream>

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

ScriptContext::ScriptContext(const std::string& scriptId, const std::string& filepath) 
    : m_scriptId(scriptId)
    , m_filepath(filepath) {
}

ScriptContext::~ScriptContext() {
    // TODO: Clean up Lua references when we have proper lifetime management
}

bool ScriptContext::load(lua_State* L) {
    // Load the script file
    if (luaL_loadfile(L, m_filepath.c_str()) != LUA_OK) {
        std::cerr << "Error loading script " << m_scriptId << ": " 
                  << lua_tostring(L, -1) << std::endl;
        lua_pop(L, 1);
        return false;
    }
    
    m_loaded = true;
    return true;
}

bool ScriptContext::execute(lua_State* L) {
    if (!m_loaded) {
        std::cerr << "Script " << m_scriptId << " not loaded" << std::endl;
        return false;
    }
    
    // The compiled chunk should be on the stack from load()
    // Execute it with 0 arguments, expecting 1 return value
    if (lua_pcall(L, 0, 1, 0) != LUA_OK) {
        std::cerr << "Error executing script " << m_scriptId << ": " 
                  << lua_tostring(L, -1) << std::endl;
        lua_pop(L, 1);
        return false;
    }
    
    // Leave the return value on the stack
    return true;
}

void ScriptContext::addEventHandler(const std::string& event, int ref) {
    m_eventHandlers[event].push_back(ref);
}

const std::vector<int>& ScriptContext::getEventHandlers(const std::string& event) const {
    static const std::vector<int> empty;
    auto it = m_eventHandlers.find(event);
    return (it != m_eventHandlers.end()) ? it->second : empty;
}