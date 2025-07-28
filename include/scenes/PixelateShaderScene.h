#ifndef PIXELATE_SHADER_SCENE_H
#define PIXELATE_SHADER_SCENE_H

#include "Scene.h"
#include "ResourceManager.h"
#include "raylib.h"

class PixelateShaderScene : public Scene {
public:
    PixelateShaderScene();
    ~PixelateShaderScene();
    
    void onEnter() override;
    void onExit() override;
    void update(float deltaTime) override;
    void render() override;
    void processInput() override;
    bool needsContinuousRendering() const override { return true; }
    
private:
    RenderTexture2D renderTarget;
    Shader pixelateShader;
    float timeValue = 0.0f;
    
    void drawDemoContent();
};

#endif // PIXELATE_SHADER_SCENE_H