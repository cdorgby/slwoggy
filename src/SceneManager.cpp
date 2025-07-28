#include "SceneManager.h"
#include "InputManager.h"
#include "raylib.h"
#include <iostream>

SceneManager& SceneManager::getInstance() {
    static SceneManager instance;
    return instance;
}

void SceneManager::registerScene(const std::string& name, SceneFactory factory) {
    sceneFactories[name] = factory;
}

std::unique_ptr<Scene> SceneManager::createScene(const std::string& name) {
    auto it = sceneFactories.find(name);
    if (it != sceneFactories.end()) {
        auto scene = it->second();
        scene->setSceneManager(this);
        return scene;
    }
    
    std::cerr << "Scene not found: " << name << std::endl;
    return nullptr;
}

void SceneManager::changeScene(const std::string& sceneName, TransitionType transition) {
    auto newScene = createScene(sceneName);
    if (!newScene) return;
    
    // Set up transition
    currentTransition.type = transition;
    currentTransition.elapsed = 0.0f;
    currentTransition.active = (transition != TransitionType::NONE);
    
    // Store pending scene and action
    pendingScene = std::move(newScene);
    pendingAction = PendingAction::CHANGE;
    
    // If no transition, execute immediately
    if (transition == TransitionType::NONE) {
        executePendingAction();
    }
}

void SceneManager::pushScene(const std::string& sceneName, TransitionType transition) {
    std::cout << "[SceneManager::pushScene] Pushing scene: " << sceneName << ", current stack size: " << sceneStack.size() << std::endl;
    auto newScene = createScene(sceneName);
    if (!newScene) return;
    
    // Set up transition
    currentTransition.type = transition;
    currentTransition.elapsed = 0.0f;
    currentTransition.active = (transition != TransitionType::NONE);
    
    // Store pending scene and action
    pendingScene = std::move(newScene);
    pendingAction = PendingAction::PUSH;
    
    // If no transition, execute immediately
    if (transition == TransitionType::NONE) {
        executePendingAction();
    }
}

void SceneManager::popScene(TransitionType transition) {
    std::cout << "[SceneManager::popScene] Stack size: " << sceneStack.size() << std::endl;
    
    if (sceneStack.empty()) {
        // No scenes to pop
        return;
    }
    
    if (sceneStack.size() == 1) {
        // Only one scene left - this is the root scene, so quit
        std::cout << "[SceneManager::popScene] Only root scene left, requesting quit" << std::endl;
        requestQuit();
        return;
    }
    
    // Set up transition
    currentTransition.type = transition;
    currentTransition.elapsed = 0.0f;
    currentTransition.active = (transition != TransitionType::NONE);
    
    pendingAction = PendingAction::POP;
    
    // If no transition, execute immediately
    if (transition == TransitionType::NONE) {
        executePendingAction();
    }
}

void SceneManager::popToRoot() {
    while (sceneStack.size() > 1) {
        sceneStack.pop();
    }
    
    if (!sceneStack.empty()) {
        sceneStack.top()->onResume();
        sceneStack.top()->setState(SceneState::ACTIVE);
    }
}

void SceneManager::executePendingAction() {
    std::cout << "[SceneManager::executePendingAction] Action: " << static_cast<int>(pendingAction) << ", stack size before: " << sceneStack.size() << std::endl;
    
    switch (pendingAction) {
        case PendingAction::CHANGE:
            // Clear the entire stack
            while (!sceneStack.empty()) {
                sceneStack.top()->onExit();
                sceneStack.pop();
            }
            
            // Add new scene
            if (pendingScene) {
                pendingScene->onEnter();
                pendingScene->setState(SceneState::ACTIVE);
                sceneStack.push(std::move(pendingScene));
            }
            break;
            
        case PendingAction::PUSH:
            // Suspend current scene
            if (!sceneStack.empty()) {
                sceneStack.top()->onSuspend();
            }
            
            // Add new scene
            if (pendingScene) {
                pendingScene->onEnter();
                pendingScene->setState(SceneState::ACTIVE);
                sceneStack.push(std::move(pendingScene));
                std::cout << "[SceneManager::executePendingAction] PUSH complete, stack size: " << sceneStack.size() << std::endl;
            }
            break;
            
        case PendingAction::POP:
            if (!sceneStack.empty()) {
                sceneStack.top()->onExit();
                sceneStack.pop();
                std::cout << "[SceneManager::executePendingAction] POP complete, stack size: " << sceneStack.size() << std::endl;
                
                // Resume previous scene
                if (!sceneStack.empty()) {
                    sceneStack.top()->onResume();
                    sceneStack.top()->setState(SceneState::ACTIVE);
                }
            }
            break;
            
        case PendingAction::NONE:
            break;
    }
    
    pendingAction = PendingAction::NONE;
    pendingScene = nullptr;
}

void SceneManager::update(float deltaTime) {
    // Update transition
    if (currentTransition.active) {
        updateTransition(deltaTime);
    }
    
    // Update current scene
    if (!sceneStack.empty() && sceneStack.top()->getState() == SceneState::ACTIVE) {
        sceneStack.top()->update(deltaTime);
    }
}

void SceneManager::updateTransition(float deltaTime) {
    currentTransition.update(deltaTime);
    
    // Execute pending action at midpoint of transition
    if (currentTransition.getProgress() >= 0.5f && pendingAction != PendingAction::NONE) {
        executePendingAction();
    }
    
    // End transition
    if (currentTransition.isComplete()) {
        currentTransition.active = false;
    }
}

void SceneManager::render() {
    // Check if we should render underlying scene
    bool renderUnderlying = false;
    if (!sceneStack.empty()) {
        if (auto overlay = dynamic_cast<OverlayScene*>(sceneStack.top().get())) {
            renderUnderlying = overlay->shouldRenderUnderlying();
        }
    }
    
    // Render scenes
    if (renderUnderlying && sceneStack.size() > 1) {
        // TODO: Implement proper underlying scene rendering
        // For now, overlay scenes will just render on top
    }
    
    // Render current scene
    if (!sceneStack.empty()) {
        sceneStack.top()->render();
    }
    
    // Render transition effect
    if (currentTransition.active) {
        renderTransition();
    }
}

void SceneManager::renderTransition() {
    float progress = currentTransition.getProgress();
    
    switch (currentTransition.type) {
        case TransitionType::FADE:
            DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), 
                         Fade(BLACK, progress < 0.5f ? progress * 2 : (1 - progress) * 2));
            break;
            
        case TransitionType::SLIDE_LEFT:
            // Implement slide transitions
            break;
            
        case TransitionType::SLIDE_RIGHT:
            // Implement slide transitions
            break;
            
        case TransitionType::ZOOM_IN:
            // Implement zoom transitions
            break;
            
        case TransitionType::ZOOM_OUT:
            // Implement zoom transitions
            break;
            
        case TransitionType::NONE:
        default:
            break;
    }
}

void SceneManager::processInput() {
    // Don't process input during transitions
    if (currentTransition.active) {
        return;
    }
    
    // Let current scene process input
    if (!sceneStack.empty() && sceneStack.top()->getState() == SceneState::ACTIVE) {
        sceneStack.top()->processInput();
    }
    
    // Check for global scene management inputs
    auto& input = InputManager::getInstance();
    
    if (input.isActionPressed("navigate_home")) {
        popToRoot();
    }
    
    if (input.isActionPressed("quit")) {
        requestQuit();
    }
}

Scene* SceneManager::getCurrentScene() const {
    if (sceneStack.empty()) {
        return nullptr;
    }
    return sceneStack.top().get();
}

bool SceneManager::currentSceneNeedsContinuousRendering() const {
    if (sceneStack.empty()) {
        return false;
    }
    return sceneStack.top()->needsContinuousRendering();
}