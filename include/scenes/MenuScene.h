#ifndef MENU_SCENE_H
#define MENU_SCENE_H

#include "Scene.h"
#include <vector>
#include <string>

struct MenuItem {
    std::string text;
    std::string sceneName;
    bool enabled;
};

class MenuScene : public Scene {
public:
    MenuScene();
    
    void onEnter() override;
    void update(float deltaTime) override;
    void render() override;
    void processInput() override;
    
private:
    std::vector<MenuItem> menuItems;
    int selectedIndex = 0;
    
    // Visual settings
    const int itemHeight = 40;
    const int itemSpacing = 10;
    const int startY = 200;
    
    void selectItem(int index);
    void activateSelectedItem();
};

#endif // MENU_SCENE_H