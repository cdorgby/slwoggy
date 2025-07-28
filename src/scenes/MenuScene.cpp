#include "scenes/MenuScene.h"
#include "SceneManager.h"
#include "InputManager.h"
#include "raylib.h"

MenuScene::MenuScene() : Scene("MainMenu") {
    // Initialize menu items
    menuItems = {
        {"Wave Shader Demo", "WaveShader", true},
        {"Pixelate Shader Demo", "PixelateShader", true},
        {"Data Center Editor", "DataCenterEditor", false}, // Coming soon
        {"Exit", "", true}
    };
}

void MenuScene::onEnter() {
    Scene::onEnter();
    selectedIndex = 0;
}

void MenuScene::update(float deltaTime) {
    // Menu animations could go here
}

void MenuScene::render() {
    BeginDrawing();
    ClearBackground(RAYWHITE);
    
    // Title
    const char* title = "Raylib Demo Suite";
    int titleWidth = MeasureText(title, 40);
    DrawText(title, GetScreenWidth()/2 - titleWidth/2, 80, 40, DARKGRAY);
    
    // Subtitle
    const char* subtitle = "Select a demo:";
    int subtitleWidth = MeasureText(subtitle, 20);
    DrawText(subtitle, GetScreenWidth()/2 - subtitleWidth/2, 140, 20, GRAY);
    
    // Menu items
    for (size_t i = 0; i < menuItems.size(); i++) {
        int y = startY + i * (itemHeight + itemSpacing);
        
        // Background for selected item
        if (i == selectedIndex) {
            DrawRectangle(GetScreenWidth()/2 - 200, y - 5, 400, itemHeight, LIGHTGRAY);
        }
        
        // Text color based on state
        Color textColor = menuItems[i].enabled ? BLACK : GRAY;
        if (i == selectedIndex && menuItems[i].enabled) {
            textColor = BLUE;
        }
        
        // Draw item text
        int textWidth = MeasureText(menuItems[i].text.c_str(), 24);
        DrawText(menuItems[i].text.c_str(), 
                GetScreenWidth()/2 - textWidth/2, y, 24, textColor);
        
        // Draw disabled indicator
        if (!menuItems[i].enabled) {
            DrawText("(Coming Soon)", 
                    GetScreenWidth()/2 + textWidth/2 + 10, y, 16, GRAY);
        }
    }
    
    // Instructions
    DrawText("Use UP/DOWN arrows or number keys to select", 
            GetScreenWidth()/2 - 200, GetScreenHeight() - 80, 16, DARKGRAY);
    DrawText("Press ENTER to confirm", 
            GetScreenWidth()/2 - 100, GetScreenHeight() - 50, 16, DARKGRAY);
    
    EndDrawing();
}

void MenuScene::processInput() {
    auto& input = InputManager::getInstance();
    
    // Navigation
    if (input.isActionPressed("navigate_back")) {
        // Menu is root scene, so quit on back
        if (sceneManager) {
            sceneManager->requestQuit();
        }
    }
    
    // Menu navigation
    if (input.isActionPressed("menu_up")) {
        selectItem(selectedIndex - 1);
    }
    if (input.isActionPressed("menu_down")) {
        selectItem(selectedIndex + 1);
    }
    
    // Confirm selection
    if (input.isActionPressed("confirm") || input.isActionPressed("confirm_alt")) {
        activateSelectedItem();
    }
    
    // Number key selection
    if (input.isActionPressed("select_1") && menuItems.size() > 0) {
        selectItem(0);
        activateSelectedItem();
    }
    if (input.isActionPressed("select_2") && menuItems.size() > 1) {
        selectItem(1);
        activateSelectedItem();
    }
    if (input.isActionPressed("select_3") && menuItems.size() > 2) {
        selectItem(2);
        activateSelectedItem();
    }
    if (input.isActionPressed("select_4") && menuItems.size() > 3) {
        selectItem(3);
        activateSelectedItem();
    }
    if (input.isActionPressed("select_5") && menuItems.size() > 4) {
        selectItem(4);
        activateSelectedItem();
    }
}

void MenuScene::selectItem(int index) {
    if (index >= 0 && index < menuItems.size()) {
        selectedIndex = index;
    }
}

void MenuScene::activateSelectedItem() {
    if (selectedIndex < 0 || selectedIndex >= menuItems.size()) return;
    
    const auto& item = menuItems[selectedIndex];
    
    if (!item.enabled) return;
    
    if (item.sceneName.empty()) {
        // Exit option - request quit
        if (sceneManager) {
            sceneManager->requestQuit();
        }
    } else {
        // Change to selected scene
        requestSceneChange(item.sceneName, true); // Push scene
    }
}