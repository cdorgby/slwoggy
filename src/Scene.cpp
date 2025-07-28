#include "Scene.h"
#include "SceneManager.h"

void Scene::requestSceneChange(const std::string& sceneName, bool push) {
    if (sceneManager) {
        if (push) {
            sceneManager->pushScene(sceneName, TransitionType::FADE);
        } else {
            sceneManager->changeScene(sceneName, TransitionType::FADE);
        }
    }
}

void Scene::requestPopScene() {
    if (sceneManager) {
        sceneManager->popScene(TransitionType::FADE);
    }
}