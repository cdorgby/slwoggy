#include "scenes/PixelateShaderScene.h"
#include "InputManager.h"

PixelateShaderScene::PixelateShaderScene() : Scene("PixelateShader") {
    pixelateShader = {0};
}

PixelateShaderScene::~PixelateShaderScene() {
    if (renderTarget.id > 0) {
        UnloadRenderTexture(renderTarget);
    }
    // Don't unload shader - ResourceManager owns cached shaders
}

void PixelateShaderScene::onEnter() {
    Scene::onEnter();
    
    // Create render texture
    renderTarget = LoadRenderTexture(GetScreenWidth(), GetScreenHeight());
    
    // Load pixelate shader
    pixelateShader = ResourceManager::getInstance().loadShader("", "pixelate.fs");
    
    if (pixelateShader.id > 0) {
        // Set resolution uniform
        Vector2 resolution = { (float)GetScreenWidth(), (float)GetScreenHeight() };
        SetShaderValue(pixelateShader, GetShaderLocation(pixelateShader, "resolution"), 
                      &resolution, SHADER_UNIFORM_VEC2);
    } else {
        TraceLog(LOG_WARNING, "Failed to load pixelate shader");
    }
}

void PixelateShaderScene::onExit() {
    Scene::onExit();
    
    // Cleanup happens in destructor
}

void PixelateShaderScene::update(float deltaTime) {
    timeValue += deltaTime;
    
    // Update shader time uniform
    if (pixelateShader.id > 0) {
        SetShaderValue(pixelateShader, GetShaderLocation(pixelateShader, "time"), 
                      &timeValue, SHADER_UNIFORM_FLOAT);
    }
}

void PixelateShaderScene::render() {
    // Render to texture
    BeginTextureMode(renderTarget);
        drawDemoContent();
    EndTextureMode();
    
    // Draw with shader
    BeginDrawing();
        ClearBackground(BLACK);
        
        if (pixelateShader.id > 0) {
            BeginShaderMode(pixelateShader);
        }
        
        // Draw the render texture (flipped because of OpenGL coordinates)
        DrawTextureRec(renderTarget.texture, 
                      (Rectangle){0, 0, (float)renderTarget.texture.width, 
                                 (float)-renderTarget.texture.height},
                      (Vector2){0, 0}, WHITE);
        
        if (pixelateShader.id > 0) {
            EndShaderMode();
        }
        
        // UI overlay
        DrawText("Pixelate Shader Demo", 10, 10, 30, WHITE);
        DrawText("Press ESC to return to menu", 10, GetScreenHeight() - 30, 20, WHITE);
        
    EndDrawing();
}

void PixelateShaderScene::drawDemoContent() {
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
    
    DrawText("PIXELATE EFFECT", screenWidth/2 - 120, screenHeight/2 - 20, 30, WHITE);
}

void PixelateShaderScene::processInput() {
    auto& input = InputManager::getInstance();
    
    if (input.isActionPressed("navigate_back")) {
        requestPopScene();
    }
}