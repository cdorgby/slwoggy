#pragma once

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

#include <memory>
#include <string>

// Wrapper around lua_State for future thread isolation
class LuaState {
public:
    LuaState();
    ~LuaState();
    
    // No copying
    LuaState(const LuaState&) = delete;
    LuaState& operator=(const LuaState&) = delete;
    
    // Get raw Lua state
    lua_State* get() const { return L; }
    operator lua_State*() const { return L; }
    
    // Initialize global tables
    void registerGlobals();
    
    // Thread/worker ID for future multi-threading
    void setThreadId(int id) { threadId = id; }
    int getThreadId() const { return threadId; }
    
private:
    lua_State* L;
    int threadId;
    
    // Register individual global tables
    void registerScriptTable();
    void registerDefinesTable();
    void registerGameTable();
    void registerDataTable();
    void registerTranslateTable();
    void registerRequireFunction();
};