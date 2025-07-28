# UI System Redesign

## Current Problems
1. All UI elements auto-register globally when created
2. Memory leaks if elements created but not parented
3. No clear ownership model
4. Frame is not a proper container

## New Design

### Frame as Container
```lua
-- Frame is a container that owns its children
local mainMenu = UI.Frame.new({
    name = "mainmenu",
    layout = "vertical"
})

-- Frame exposes its own API for creating children
local title = mainMenu:Label({
    text = "Data Center Operations",
    fontSize = 32
})

local waveBtn = mainMenu:Button({
    text = "Wave Shader Demo"
})

-- Only top-level frames register with the application
mainMenu:show()  -- This registers with ApplicationView
```

### Benefits
1. **Clear ownership**: Frame owns all children it creates
2. **No orphans**: Children can't exist without parent
3. **Encapsulation**: Frame controls its own UI tree
4. **Explicit registration**: Only shown frames register globally

### Implementation
- Frame should have factory methods: `Label()`, `Button()`, `Frame()`
- Children are created already parented
- Only `show()` registers with ApplicationView
- `hide()` unregisters from ApplicationView
- When frame is destroyed, all children are cleaned up

### Alternative Syntax
```lua
local mainMenu = UI.Frame.new({
    name = "mainmenu",
    layout = "vertical",
    children = {
        UI.Label { text = "Data Center Operations" },
        UI.Button { 
            text = "Wave Shader",
            onClick = function() ... end
        }
    }
})
```