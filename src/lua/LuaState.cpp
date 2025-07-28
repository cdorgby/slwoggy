#include "lua/LuaState.h"
#include "lua/LuaHelpers.h"
#include "lua/LuaManager.h"
#include <iostream>

LuaState::LuaState() : threadId(0) {
    L = luaL_newstate();
    if (!L) {
        throw std::runtime_error("Failed to create Lua state");
    }
    
    // Open only safe libraries for sandboxing
    luaopen_base(L);
    lua_pop(L, 1);
    luaopen_table(L);
    lua_pop(L, 1);
    luaopen_string(L);
    lua_pop(L, 1);
    luaopen_math(L);
    lua_pop(L, 1);
    
    // Remove dangerous base functions
    lua_pushnil(L);
    lua_setglobal(L, "dofile");
    lua_pushnil(L);
    lua_setglobal(L, "loadfile");
    lua_pushnil(L);
    lua_setglobal(L, "load");
    lua_pushnil(L);
    lua_setglobal(L, "loadstring");
    lua_pushnil(L);
    lua_setglobal(L, "rawget");
    lua_pushnil(L);
    lua_setglobal(L, "rawset");
    lua_pushnil(L);
    lua_setglobal(L, "rawequal");
    lua_pushnil(L);
    lua_setglobal(L, "rawlen");
    lua_pushnil(L);
    lua_setglobal(L, "getfenv");
    lua_pushnil(L);
    lua_setglobal(L, "setfenv");
}

LuaState::~LuaState() {
    if (L) {
        lua_close(L);
    }
}

void LuaState::registerGlobals() {
    registerScriptTable();
    registerDefinesTable();
    registerGameTable();
    registerDataTable();
    registerTranslateTable();
    registerRequireFunction();
}

void LuaState::registerScriptTable() {
    lua_create_table(L, "script");
    
    // Register script functions
    lua_register_function(L, "on_init", LuaManager::script_on_init);
    lua_register_function(L, "on_event", LuaManager::script_on_event);
    
    lua_pop(L, 1); // Pop script table
}

void LuaState::registerDefinesTable() {
    lua_create_table(L, "defines");
    
    // Create events subtable
    lua_newtable(L);
    lua_setfield(L, -2, "events");
    
    lua_pop(L, 1); // Pop defines table
}

void LuaState::registerGameTable() {
    lua_create_table(L, "game");
    
    // Register game functions
    lua_register_function(L, "print", LuaManager::game_print);
    lua_register_function(L, "get_pawn", LuaManager::game_get_pawn);
    lua_register_function(L, "exit", LuaManager::game_exit);
    
    lua_pop(L, 1); // Pop game table
}

void LuaState::registerDataTable() {
    // Create data table with metatable for :extend syntax
    lua_newtable(L);
    
    // Create metatable
    lua_newtable(L);
    
    // Set __index to support data:extend()
    lua_pushstring(L, "__index");
    lua_newtable(L);
    lua_register_function(L, "extend", LuaManager::data_extend);
    lua_settable(L, -3);
    
    // Apply metatable
    lua_setmetatable(L, -2);
    
    // Create data.raw table
    lua_newtable(L);
    lua_setfield(L, -2, "raw");
    
    // Set as global
    lua_setglobal(L, "data");
}

void LuaState::registerTranslateTable() {
    // Create translate table that gracefully handles missing translations
    // Strategy: Return empty tables for intermediate keys, but with a special
    // metatable that tracks depth and eventually returns nil
    
    // Create the metatable that will be shared by all translate tables
    lua_newtable(L);
    
    // __index metamethod
    lua_pushstring(L, "__index");
    lua_pushcfunction(L, [](lua_State* L) -> int {
        // Return an empty table with the same metatable
        lua_newtable(L);
        lua_getmetatable(L, 1); // Get parent's metatable
        lua_setmetatable(L, -2); // Set on new table
        return 1;
    });
    lua_settable(L, -3);
    
    // Store metatable in registry
    lua_pushvalue(L, -1);
    lua_setfield(L, LUA_REGISTRYINDEX, "translate_mt");
    
    // Create main translate table
    lua_newtable(L);
    lua_pushvalue(L, -2); // Push metatable
    lua_setmetatable(L, -2);
    
    // For now, let's pre-populate the expected structure to avoid the issue
    // This is temporary until we have proper translation loading
    
    // translate.dorgby = {}
    lua_newtable(L);
    lua_getfield(L, LUA_REGISTRYINDEX, "translate_mt");
    lua_setmetatable(L, -2);
    lua_setfield(L, -2, "dorgby");
    
    // translate.dorgby.dco = {}
    lua_getfield(L, -1, "dorgby");
    lua_newtable(L);
    lua_getfield(L, LUA_REGISTRYINDEX, "translate_mt");
    lua_setmetatable(L, -2);
    lua_setfield(L, -2, "dco");
    lua_pop(L, 1);
    
    // Set as global
    lua_setglobal(L, "translate");
    
    // Clean up
    lua_pop(L, 1); // Pop metatable
}

void LuaState::registerRequireFunction() {
    // Create minimal package table with loaded subtable
    lua_newtable(L);
    
    // Create package.loaded table
    lua_newtable(L);
    lua_setfield(L, -2, "loaded");
    
    // Set as global
    lua_setglobal(L, "package");
    
    // Register custom require function
    lua_pushcfunction(L, LuaManager::lua_require);
    lua_setglobal(L, "require");
}