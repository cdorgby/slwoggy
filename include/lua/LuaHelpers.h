#pragma once

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

// Macro helpers for cleaner Lua API
#define LUA_FUNCTION(name) static int name(lua_State* L)

#define LUA_CHECK_ARGS(count) \
    if (lua_gettop(L) != count) { \
        return luaL_error(L, "%s: Expected %d arguments, got %d", __FUNCTION__, count, lua_gettop(L)); \
    }

#define LUA_CHECK_MIN_ARGS(count) \
    if (lua_gettop(L) < count) { \
        return luaL_error(L, "%s: Expected at least %d arguments, got %d", __FUNCTION__, count, lua_gettop(L)); \
    }

#define LUA_CHECK_FUNCTION(idx) \
    if (!lua_isfunction(L, idx)) { \
        return luaL_error(L, "%s: Expected function at argument %d", __FUNCTION__, idx); \
    }

#define LUA_CHECK_TABLE(idx) \
    if (!lua_istable(L, idx)) { \
        return luaL_error(L, "%s: Expected table at argument %d", __FUNCTION__, idx); \
    }

#define LUA_CHECK_STRING(idx) \
    if (!lua_isstring(L, idx)) { \
        return luaL_error(L, "%s: Expected string at argument %d", __FUNCTION__, idx); \
    }

#define LUA_CHECK_NUMBER(idx) \
    if (!lua_isnumber(L, idx)) { \
        return luaL_error(L, "%s: Expected number at argument %d", __FUNCTION__, idx); \
    }

// Register a C function to a Lua table
inline void lua_register_function(lua_State* L, const char* name, lua_CFunction func) {
    lua_pushcfunction(L, func);
    lua_setfield(L, -2, name);
}

// Create a new table and leave on stack
inline void lua_create_table(lua_State* L, const char* name) {
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_setglobal(L, name);
}

// Push a simple error
inline int lua_error_simple(lua_State* L, const char* msg) {
    lua_pushnil(L);
    lua_pushstring(L, msg);
    return 2;
}