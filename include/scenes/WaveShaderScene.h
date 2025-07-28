#ifndef WAVE_SHADER_SCENE_H
#define WAVE_SHADER_SCENE_H

#include "Scene.h"
#include "ResourceManager.h"
#include "raylib.h"

class WaveShaderScene : public Scene {
public:
    WaveShaderScene();
    ~WaveShaderScene();
    
    void onEnter() override;
    void onExit() override;
    void update(float deltaTime) override;
    void render() override;
    void processInput() override;
    bool needsContinuousRendering() const override { return true; }
    
private:
    RenderTexture2D renderTarget;
    Shader waveShader;
    float timeValue = 0.0f;
    
    void drawDemoContent();
};

#endif // WAVE_SHADER_SCENE_H