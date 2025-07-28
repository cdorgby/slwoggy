# Lua Integration Design Document

## Overview

This document describes the Lua scripting integration for the CDO project, inspired by Factorio's modding architecture. The system provides a safe, performant API boundary between C++ core systems and Lua scripts, enabling extensive modding capabilities while maintaining control over performance-critical paths.

## Architecture

### Core Principles

1. **API Boundary Safety**: Lua scripts cannot directly access C++ memory or STL structures
2. **Event-Driven**: Communication happens through events and callbacks
3. **Performance First**: Hot paths remain in C++, Lua handles logic and configuration
4. **Automatic Cleanup**: Entity destruction automatically cleans up associated Lua state

### System Components

```
┌─────────────┐     ┌──────────────┐     ┌─────────────┐
│   Lua Scripts│────▶│  API Layer   │────▶│ C++ Core    │
│   (Logic)    │◀────│  (Safe)      │◀────│ (Fast)      │
└─────────────┘     └──────────────┘     └─────────────┘
```

## Implementation

### 1. LuaObject Base Class

All Lua-exposed objects inherit from `LuaObject`:

```cpp
class LuaObject {
protected:
    int luaRef = LUA_NOREF;     // Reference in Lua registry
    bool valid = true;          // Invalidated when C++ object destroyed
    lua_State* L = nullptr;     // Lua state that owns this object
    
public:
    virtual ~LuaObject();       // Cleans up Lua references
    void invalidate();          // Called when C++ object is destroyed
    bool isValid() const { return valid; }
};
```

### 2. Macro-Based Binding System

Inspired by Factorio's approach, we use macros to reduce boilerplate:

```cpp
class LuaEntity : public LuaObject {
    DECLARE_LUA_CLASS(LuaEntity);
    
    // Properties
    LUA_PROPERTY(position, getPosition, setPosition);
    LUA_PROPERTY(health, getHealth, setHealth);
    LUA_READONLY(id);
    LUA_READONLY(valid);
    
    // Methods
    LUA_METHOD(destroy);
    LUA_METHOD(teleport);
    
private:
    EntityID entityId;  // Reference to actual entity
    
    LuaTable getPosition();
    void setPosition(LuaTable pos);
    // etc...
};
```

### 3. Event System

#### Global Events

```lua
-- Lua script registers for global events
register_event("on_tick", function(event)
    -- Called every game tick
end)

register_event("on_entity_created", function(event)
    local entity = event.entity
    -- React to entity creation
end)
```

#### Entity Events

```lua
-- Register handler on specific entity
local button = ui.create_button{text = "Click me"}

button:on("click", function()
    print("Button clicked!")
end)

button:on("hover_enter", function()
    button:setColor("hovered", {255, 255, 0})
end)
```

### 4. UI System

#### UI Entity Hierarchy

```
LuaUIEntity (base)
├── LuaButton
├── LuaPanel
├── LuaLabel
├── LuaTextInput
└── LuaScrollArea
```

#### Layout System

```lua
local panel = ui.create_panel{
    layout = "vertical",
    padding = 10,
    spacing = 5
}

-- Children automatically positioned
panel:add_child(ui.create_label{text = "Settings"})
panel:add_child(ui.create_button{text = "Apply"})
```

## Memory Management

### Ownership Rules

1. **C++ Owns Objects**: All game objects are owned by C++
2. **Lua Has References**: Lua holds weak references via registry
3. **Automatic Invalidation**: When C++ deletes object, Lua reference becomes invalid
4. **Event Cleanup**: Entity destruction cleans up all registered event handlers

### Reference Lifecycle

```cpp
// C++ creates entity
auto entity = world->createEntity();
auto luaEntity = new LuaEntity(entity->getId());

// Push to Lua
lua_pushlightuserdata(L, luaEntity);

// Later: C++ destroys entity
entity->destroy();
luaEntity->invalidate();  // All Lua operations now return nil/error
```

## API Examples

### Creating UI Elements

```lua
-- Create a button with constraints
local button = ui.create_button{
    text = "Start Game",
    parent = ui.root,
    constraints = {
        center_x = true,
        y = 100,
        width = 200,
        height = 50
    }
}

-- Style the button
button:setStyle{
    normal = {
        background = {64, 64, 64},
        text_color = {255, 255, 255},
        border = {128, 128, 128}
    },
    hovered = {
        background = {96, 96, 96},
        text_color = {255, 255, 255},
        border = {192, 192, 192}
    }
}
```

### Event Handling

```lua
-- Global event
register_event("on_game_start", function()
    print("Game started!")
    
    -- Create UI
    local menu = ui.create_panel{...}
    
    -- Return cleanup function (optional)
    return function()
        menu:destroy()
    end
end)

-- Entity-specific event with state
local counter = 0
button:on("click", function()
    counter = counter + 1
    button:setText("Clicked " .. counter .. " times")
end)
```

### Protected API Access

```lua
-- Can't access internals
local entity = game.get_entity(123)
-- entity.internalPtr  -- Error: no such field
-- entity.position.x = nil  -- Error: position is read-only table

-- Must use API methods
entity:teleport({100, 200})  -- Validated by C++
local pos = entity.position   -- Returns copy as table
```

## Performance Considerations

### Hot Path Optimization

```cpp
// C++ update loop - no Lua calls
void World::update(float dt) {
    for (auto& entity : entities) {
        entity->update(dt);  // Pure C++
    }
    
    // Only call Lua for events
    if (significantEvents.any()) {
        luaEventSystem.raise("on_world_update", significantEvents);
    }
}
```

### Batched Operations

```lua
-- Bad: Individual calls
for i = 1, 1000 do
    game.create_entity{type = "tree", position = {i, 0}}
end

-- Good: Batched call
game.create_entities{
    type = "tree",
    positions = generate_positions(1000)
}
```

## Error Handling

### Lua Errors

```cpp
// All Lua calls use protected mode
int result = lua_pcall(L, nargs, nresults, errorHandler);
if (result != LUA_OK) {
    const char* error = lua_tostring(L, -1);
    logError("Lua error: %s", error);
    lua_pop(L, 1);
    // Game continues running
}
```

### Invalid References

```lua
local entity = game.get_entity(123)
entity:destroy()

-- Later...
if entity.valid then  -- Always check validity
    entity:teleport({0, 0})
else
    print("Entity no longer exists")
end
```

## Best Practices

### For Modders

1. **Always check `valid`** before using entities
2. **Cache frequently used values** to avoid API calls
3. **Use batch operations** when possible
4. **Clean up event handlers** when done
5. **Avoid per-frame operations** in Lua

### For Core Developers

1. **Validate all inputs** from Lua
2. **Use `LUA_METHOD` macros** for consistency
3. **Document API changes** in migration guide
4. **Profile Lua impact** on performance
5. **Provide batch APIs** for common operations

## Future Enhancements

1. **Hot Reloading**: Reload Lua scripts without restart
2. **Sandboxing**: Limit CPU/memory usage per mod
3. **Debugging**: Integrated Lua debugger
4. **Profiling**: Per-mod performance metrics
5. **Networking**: Synchronized Lua state for multiplayer