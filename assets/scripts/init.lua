
-- defines, game, script, pawns, data are always provided by the game engine

require("test.test") --  load test.lua from the test directory

script:on_init(function()
    -- Create main frame
    local frame = game.get_pawn(0).gui.add_frame({
        name = "main_frame",
        caption = translate.dorgby.cdo.main_frame.caption or "Data Center Operations",
        direction = "vertical",
        style = "frame_style",
    })

    -- Frame initialization - ensures thread-safe setup before visibility
    frame:on_init(function(event)
        game.print("Frame initialized: " .. event.element.name)
        
        -- Create start button as child of frame
        local startb = event.element:add_button({
            name = "start_button",
            caption = translate.dorgby.cdo.start_button.caption or "Start Operations",
            style = "button_style",
        })

        -- Button initialization - atomic handler setup
        startb:on_init(function(event)
            game.print("Start button initialized: " .. event.element.name)
            
            -- Register all handlers during init phase
            event.element:on_click(function(click_event)
                game.print("Start button clicked: " .. click_event.element.caption)
                -- Add logic to handle start button click
            end)
            
            event.element:on_input("navigate_back", function(input_event)
                game.print("Navigating back...")
                -- Add logic to navigate back
            end)
        end)
        
        -- Create exit button
        local exitb = event.element:add_button({
            name = "exit_button",
            caption = translate.dorgby.cdo.exit_button.caption or "Exit",
            style = "button_style",
        })
        
        exitb:on_init(function(event)
            event.element:on_click(function(click_event)
                game.exit()
            end)
        end)
    end)
end)

script:on_event(defines.events.on_gui_click, function(event)
    if event.element.name == "start_button" then
        -- Handle start button click
        game.print("Starting operations...") -- translate.dorgby.cdo.start_button.click or "Starting operations..."
        -- Add logic to start operations here
    end
end)

data:extend({
    {
        type = "custom-input",
        name = "navigate_back",
        key_sequence = "BACKSPACE",
        consuming = "none",
    },
    {
        type = "custom-input",
        name = "open_menu",
        key_sequence = "CONTROL + M", 
        consuming = "none",
    },
    {
        -- dorgby.cdo.data.data_center_rack_a
        -- or data.raw["dorgby.cdo.data_center_rack"]
        type = "floor-entity",
        sub_type = "rack",
        name = "data_center_rack_a",
        -- implicit caption = translate.dorgby.cdo.data_center_rack_a.caption
        icon = "dorgby.cdo.graphics.icon.data_center_rack_a",
        -- width and depth are in tiles, height rack unis 1U = 1.75 cm
        size = { width = 2, depth = 4, height = 48},
        weight = 1000, -- in kg each tile is 1000 /(2 * 4) -- 125 kg per tile
    },
    {
        type = "floor-entity",
        sub_type = "table",
        name = "work_bench_a",
        caption = translate.dorgby.cdo.data_center_rack_a.caption or "Work Bench A",
        icon = "dorgby.cdo.graphics.icon.work_bench_a",
        size = { width = 6, depth = 4, height = 42},
        -- explicit weight allocation into the corners of the table (where the legs are)
        weight = {
            {250, 0, 0, 0, 0, 250},
            {0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0},
            {250, 0, 0, 0, 0, 250}, -- last row is heavier
        }
    },
})

-- Entity creation event - simplified pattern
script:on_event(defines.events.on_entity_created, function(event)
    if event.entity.type == "floor-entity" then
        game.print("Floor entity created: " .. event.entity.name)
        
        -- Entity initialization - ensures thread-safe handler setup
        event.entity.on_init(function(init_event)
            game.print("Floor entity initialized: " .. init_event.entity.name)
            
            -- Register click handler during init phase
            init_event.entity.on_click(function(click_event)
                game.print("Floor entity clicked: " .. click_event.entity.name)
                -- Add click handling logic here
            end)
            
            -- Register other handlers as needed
            init_event.entity.on_event(defined.events.on_entity_damaged, function(damage_event)
                game.print("Entity damaged: " .. damage_event.damage .. " HP")
            end)
        end)
    end
end)