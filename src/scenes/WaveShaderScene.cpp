#include "scenes/WaveShaderScene.h"
#include "InputManager.h"
#include <iostream>

WaveShaderScene::WaveShaderScene() : Scene("WaveShader") {
    waveShader = {0};
}

WaveShaderScene::~WaveShaderScene() {
    if (renderTarget.id > 0) {
        UnloadRenderTexture(renderTarget);
    }
    // Don't unload shader - ResourceManager owns cached shaders
}

void WaveShaderScene::onEnter() {
    Scene::onEnter();
    
    // Create render texture
    renderTarget = LoadRenderTexture(GetScreenWidth(), GetScreenHeight());
    
    // Load wave shader
    waveShader = ResourceManager::getInstance().loadShader("", "wave.fs");
    
    if (waveShader.id == 0) {
        // Fallback if shader fails to load
        TraceLog(LOG_WARNING, "Failed to load wave shader");
    }
}

void WaveShaderScene::onExit() {
    Scene::onExit();
    
    // Cleanup happens in destructor
}

void WaveShaderScene::update(float deltaTime) {
    timeValue += deltaTime;
    
    // Update shader time uniform
    if (waveShader.id > 0) {
        SetShaderValue(waveShader, GetShaderLocation(waveShader, "time"), 
                      &timeValue, SHADER_UNIFORM_FLOAT);
    }
}

void WaveShaderScene::render() {
    // Render to texture
    BeginTextureMode(renderTarget);
        drawDemoContent();
    EndTextureMode();
    
    // Draw with shader
    BeginDrawing();
        ClearBackground(BLACK);
        
        if (waveShader.id > 0) {
            BeginShaderMode(waveShader);
        }
        
        // Draw the render texture (flipped because of OpenGL coordinates)
        DrawTextureRec(renderTarget.texture, 
                      (Rectangle){0, 0, (float)renderTarget.texture.width, 
                                 (float)-renderTarget.texture.height},
                      (Vector2){0, 0}, WHITE);
        
        if (waveShader.id > 0) {
            EndShaderMode();
        }
        
        // UI overlay
        DrawText("Wave Shader Demo", 10, 10, 30, WHITE);
        DrawText("Press ESC to return to menu", 10, GetScreenHeight() - 30, 20, WHITE);
        
    EndDrawing();
}

void WaveShaderScene::drawDemoContent() {
    ClearBackground(RAYWHITE);
    
    int screenWidth = GetScreenWidth();
    int screenHeight = GetScreenHeight();
    
    // Draw some content to apply shader to
    DrawCircle(screenWidth/2, screenHeight/2, 150, RED);
    DrawRectangle(100, 100, 200, 150, BLUE);
    DrawTriangle(
        (Vector2){screenWidth/2.0f, 100},
        (Vector2){screenWidth/2.0f - 100, 250},
        (Vector2){screenWidth/2.0f + 100, 250},
        GREEN
    );
    
    for (int i = 0; i < 10; i++) {
        DrawCircle(100 + i * 60, 400, 20, 
                  (Color){255, (unsigned char)(100 + i * 15), 0, 255});
    }
    
    DrawText("WAVE EFFECT", screenWidth/2 - 100, screenHeight/2 - 20, 30, WHITE);
}

void WaveShaderScene::processInput() {
    auto& input = InputManager::getInstance();
    
    if (input.isActionPressed("navigate_back")) {
        std::cout << "[WaveShaderScene] Handling NAVIGATE_BACK" << std::endl;
        requestPopScene();
    }
}