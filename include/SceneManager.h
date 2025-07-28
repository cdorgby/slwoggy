#ifndef SCENE_MANAGER_H
#define SCENE_MANAGER_H

#include "Scene.h"
#include <memory>
#include <stack>
#include <unordered_map>
#include <functional>
#include <string>

// Scene factory function type
using SceneFactory = std::function<std::unique_ptr<Scene>()>;

class SceneManager {
public:
    static SceneManager& getInstance();
    
    // Scene registration
    void registerScene(const std::string& name, SceneFactory factory);
    
    // Scene management
    void changeScene(const std::string& sceneName, TransitionType transition = TransitionType::NONE);
    void pushScene(const std::string& sceneName, TransitionType transition = TransitionType::NONE);
    void popScene(TransitionType transition = TransitionType::NONE);
    void popToRoot();
    
    // Process input for current scene
    void processInput();
    
    // Update and render
    void update(float deltaTime);
    void render();
    
    // Scene queries
    Scene* getCurrentScene() const;
    bool hasScene() const { return !sceneStack.empty(); }
    size_t getStackSize() const { return sceneStack.size(); }
    
    // Application control
    bool shouldQuit() const { return quitRequested; }
    void requestQuit() { quitRequested = true; }
    
    // Rendering hints
    bool currentSceneNeedsContinuousRendering() const;
    bool isTransitioning() const { return currentTransition.active; }
    
private:
    SceneManager() = default;
    ~SceneManager() = default;
    
    SceneManager(const SceneManager&) = delete;
    SceneManager& operator=(const SceneManager&) = delete;
    
    // Scene stack
    std::stack<std::unique_ptr<Scene>> sceneStack;
    
    // Scene factories
    std::unordered_map<std::string, SceneFactory> sceneFactories;
    
    // Transition management
    TransitionState currentTransition;
    std::unique_ptr<Scene> pendingScene;
    enum class PendingAction { NONE, CHANGE, PUSH, POP } pendingAction = PendingAction::NONE;
    
    // State
    bool quitRequested = false;
    
    // Internal methods
    void executePendingAction();
    void updateTransition(float deltaTime);
    void renderTransition();
    
    // Create scene from factory
    std::unique_ptr<Scene> createScene(const std::string& name);
};

#endif // SCENE_MANAGER_H