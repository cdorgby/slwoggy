-- test/test.lua
-- Test module for require() functionality

local M = {}

M.version = "1.0.0"

function M.hello()
    game.print("Hello from test.test module!")
end

function M.add(a, b)
    return a + b
end

game.print("test.test module loaded successfully")

return M