#ifndef SCENE_H
#define SCENE_H

#include <string>
#include <memory>

// Scene lifecycle states
enum class SceneState {
    UNINITIALIZED,
    ENTERING,      // Transition in
    ACTIVE,        // Normal operation
    EXITING,       // Transition out
    SUSPENDED      // Paused but not destroyed (e.g., dialog on top)
};

// Forward declaration
class SceneManager;

// Base class for all scenes
class Scene {
public:
    Scene(const std::string& name) : sceneName(name), state(SceneState::UNINITIALIZED) {}
    virtual ~Scene() = default;
    
    // Lifecycle methods
    virtual void onEnter() { state = SceneState::ENTERING; }
    virtual void onExit() { state = SceneState::EXITING; }
    virtual void onSuspend() { state = SceneState::SUSPENDED; }
    virtual void onResume() { state = SceneState::ACTIVE; }

    // Core methods
    virtual void update(float deltaTime) = 0;
    virtual void render() = 0;
    
    // Input processing - called each frame before update
    virtual void processInput() {}
    
    // State management
    SceneState getState() const { return state; }
    void setState(SceneState newState) { state = newState; }
    
    // Scene information
    const std::string& getName() const { return sceneName; }
    
    // Rendering hints
    virtual bool needsContinuousRendering() const { return false; }
    
    // Allow scene to request transitions
    void setSceneManager(SceneManager* manager) { sceneManager = manager; }
    
protected:
    std::string sceneName;
    SceneState state;
    SceneManager* sceneManager = nullptr;
    
    // Helper to request scene changes from within a scene
    void requestSceneChange(const std::string& sceneName, bool push = false);
    void requestPopScene();
};

// Transition effects
enum class TransitionType {
    NONE,
    FADE,
    SLIDE_LEFT,
    SLIDE_RIGHT,
    ZOOM_IN,
    ZOOM_OUT
};

// Transition state for animated transitions
struct TransitionState {
    TransitionType type = TransitionType::NONE;
    float duration = 0.3f;
    float elapsed = 0.0f;
    bool active = false;

    float getProgress() const {
        if (duration <= 0) return 1.0f;
        return elapsed / duration;
    }
    
    bool isComplete() const {
        return elapsed >= duration;
    }
    
    void update(float deltaTime) {
        if (active) {
            elapsed += deltaTime;
        }
    }
};

// Overlay scenes can render on top of underlying scenes
class OverlayScene : public Scene {
public:
    OverlayScene(const std::string& name) : Scene(name) {}
    
    virtual bool shouldRenderUnderlying() const { return true; }
};

#endif // SCENE_H