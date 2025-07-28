# Data Center Layout Editor - Technical Design Document

## Table of Contents
1. [Executive Summary](#executive-summary)
2. [Core Architecture](#core-architecture)
3. [Rendering Pipeline](#rendering-pipeline)
4. [Grid System](#grid-system)
5. [Layer Management](#layer-management)
6. [Element System](#element-system)
7. [Infrastructure Routing](#infrastructure-routing)
8. [Shader Integration](#shader-integration)
9. [User Interface](#user-interface)
10. [Data Management](#data-management)
11. [Performance Optimization](#performance-optimization)
12. [Implementation Roadmap](#implementation-roadmap)

## Executive Summary

The Data Center Layout Editor is a professional floor planning tool built on raylib, designed for creating and managing data center infrastructure layouts. It features a RimWorld-style top-down view with multi-layer infrastructure planning, constraint-based placement, and real-time visualization through shader effects.

### Key Features
- Multi-layer editing (underfloor, floor, overhead)
- Zone-based floor planning
- Constraint validation system
- Infrastructure routing with capacity management
- Real-time shader effects (thermal, airflow, utilization)
- Professional CAD-like precision with game-like usability

## Core Architecture

### Technology Stack
```cpp
Foundation:
- Raylib 5.0 (rendering, input, audio)
- C++17 (modern features, filesystem)
- ResourceManager (asset loading)
- CMake (cross-platform build)

Rendering:
- 2D sprite-based with depth layers
- Multiple RenderTextures for compositing
- Custom shaders for visualization effects
- Immediate mode UI overlay
```

### High-Level Architecture
```
DataCenterEditor
├── Core Systems
│   ├── GridSystem          // Spatial grid management
│   ├── LayerManager        // Multi-layer rendering
│   ├── ConstraintValidator // Placement rules
│   └── SelectionManager    // User selection state
│
├── Element Systems
│   ├── FloorElement        // Base class for placeable items
│   ├── ElementRegistry     // Type definitions and sprites
│   └── ElementPlacer       // Placement logic
│
├── Infrastructure
│   ├── RouteManager        // Duct/cable routing
│   ├── CapacityTracker     // Resource limits
│   └── PathFinder          // A* for routing
│
└── Rendering
    ├── LayerRenderer       // Per-layer drawing
    ├── ShaderEffects       // Post-processing
    └── UIRenderer          // Immediate mode UI
```

## Rendering Pipeline

### Multi-Pass Rendering System
```cpp
class RenderPipeline {
    // Render targets for each layer
    RenderTexture2D underfloorRT;
    RenderTexture2D floorRT;
    RenderTexture2D overheadRT;
    RenderTexture2D uiRT;
    
    // Effect buffers
    RenderTexture2D thermalRT;
    RenderTexture2D airflowRT;
    
    void render() {
        // Pass 1: Render each layer to its RT
        renderUnderfloor(underfloorRT);
        renderFloor(floorRT);
        renderOverhead(overheadRT);
        
        // Pass 2: Composite layers with transparency
        BeginDrawing();
            // Draw floor (always visible)
            DrawTexture(floorRT.texture, 0, 0, WHITE);
            
            // Draw other layers based on view mode
            if (viewMode == UNDERFLOOR) {
                DrawTexture(floorRT.texture, 0, 0, Fade(WHITE, 0.3f));
                DrawTexture(underfloorRT.texture, 0, 0, WHITE);
            }
            
            // Pass 3: Apply shader effects
            if (thermalView) {
                BeginShaderMode(thermalShader);
                DrawTexture(thermalRT.texture, 0, 0, WHITE);
                EndShaderMode();
            }
            
            // Pass 4: UI overlay
            DrawTexture(uiRT.texture, 0, 0, WHITE);
        EndDrawing();
    }
};
```

### Sprite Batching Strategy
```cpp
struct SpriteBatch {
    Texture2D atlas;
    vector<DrawCall> instances;
    
    void addSprite(Rectangle src, Vector2 pos, float rotation, Color tint) {
        instances.push_back({src, pos, rotation, tint});
    }
    
    void flush() {
        // Sort by texture region for cache efficiency
        sort(instances.begin(), instances.end());
        
        // Draw all instances in one pass
        for (auto& inst : instances) {
            DrawTexturePro(atlas, inst.src, inst.dest, 
                          inst.origin, inst.rotation, inst.tint);
        }
        instances.clear();
    }
};
```

## Grid System

### Coordinate Spaces
```cpp
class GridSystem {
    static constexpr float TILE_SIZE = 32.0f; // pixels per foot
    static constexpr int GRID_WIDTH = 200;    // 200 ft
    static constexpr int GRID_HEIGHT = 100;   // 100 ft
    
    // Convert between coordinate systems
    Vector2 worldToGrid(Vector2 worldPos) {
        return {
            floor(worldPos.x / TILE_SIZE),
            floor(worldPos.y / TILE_SIZE)
        };
    }
    
    Vector2 gridToWorld(int x, int y) {
        return {
            x * TILE_SIZE + TILE_SIZE/2,
            y * TILE_SIZE + TILE_SIZE/2
        };
    }
    
    // Spatial queries
    vector<ElementID> getElementsInRect(Rectangle worldRect);
    bool isTileOccupied(int x, int y, Layer layer);
};
```

### Zone Management
```cpp
enum class ZoneType {
    NONE,
    COLD_AISLE,
    HOT_AISLE,
    LOAD_BEARING,
    STORAGE,
    NOC_OFFICE,
    UTILITY
};

class ZoneMap {
    array<array<ZoneType, GRID_HEIGHT>, GRID_WIDTH> zones;
    
    void paintZone(Rectangle area, ZoneType type) {
        // Flood fill or rectangle fill
    }
    
    bool canPlaceElement(ElementType type, GridPos pos) {
        ZoneType zone = zones[pos.x][pos.y];
        return isCompatible(type, zone);
    }
};
```

## Layer Management

### Layer System Design
```cpp
enum class Layer {
    UNDERFLOOR,  // Power, cooling ducts
    FLOOR,       // Racks, equipment
    OVERHEAD     // Cable trays, pipes
};

class LayerManager {
    struct LayerData {
        bool visible;
        float opacity;
        RenderTexture2D renderTarget;
        vector<ElementID> elements;
    };
    
    map<Layer, LayerData> layers;
    Layer activeLayer = Layer::FLOOR;
    
    void setActiveLayer(Layer layer) {
        activeLayer = layer;
        updateOpacities();
    }
    
    void renderLayer(Layer layer) {
        BeginTextureMode(layers[layer].renderTarget);
        ClearBackground(BLANK);
        
        // Render elements in this layer
        for (ElementID id : layers[layer].elements) {
            renderElement(id);
        }
        
        EndTextureMode();
    }
};
```

## Element System

### Base Element Class
```cpp
class FloorElement {
protected:
    ElementID id;
    ElementType type;
    GridRect footprint;    // Grid space occupied
    Layer layer;
    float rotation;        // 0, 90, 180, 270
    
public:
    virtual void render(SpriteBatch& batch) = 0;
    virtual bool canPlace(GridPos pos, const GridSystem& grid) = 0;
    virtual void onPlace() {}
    virtual void onRemove() {}
    
    Rectangle getWorldBounds() const;
    vector<GridPos> getOccupiedTiles() const;
};
```

### Element Types
```cpp
class Rack : public FloorElement {
    int rackUnits = 42;
    bool frontFacingCold;
    
    bool canPlace(GridPos pos, const GridSystem& grid) override {
        // Check zone is appropriate
        // Check orientation aligns with aisles
        // Check load bearing if needed
        return true;
    }
};

class CoolingUnit : public FloorElement {
    float tonsCooling;
    float cfmAirflow;
    AirflowPattern pattern;
};

class InfrastructureDuct : public FloorElement {
    DuctType type;      // Air, Power, Data
    float width;        // Variable
    float depth;        // Fixed by type
    vector<GridPos> path;
};
```

## Infrastructure Routing

### Routing System
```cpp
class InfrastructureRouter {
    struct RouteConstraints {
        float maxDepth;      // Vertical space limit
        float minBendRadius; // No sharp turns
        bool avoidZones[ZoneType::COUNT];
    };
    
    Path findRoute(GridPos start, GridPos end, RouteConstraints constraints) {
        // Modified A* that respects depth and bend radius
        AStar pathfinder;
        pathfinder.setHeuristic(manhattanDistance);
        pathfinder.setConstraints(constraints);
        return pathfinder.findPath(start, end);
    }
    
    bool validateRoute(const Path& path, DuctType type) {
        // Check depth constraints
        // Check collision with existing infrastructure
        // Verify capacity limits
        return true;
    }
};
```

### Capacity Management
```cpp
class CapacityTracker {
    struct RacewayCapacity {
        float widthFeet;
        int powerCables;
        int dataCables;
        
        bool canAddCable(CableType type, int count) {
            if (type == POWER) {
                return powerCables + count <= maxPowerCables();
            } else {
                return dataCables + count <= maxDataCables();
            }
        }
        
        int maxPowerCables() { return widthFeet * 12 / 2 * 5; }
        int maxDataCables() { return widthFeet * 50; }
    };
    
    map<ElementID, RacewayCapacity> racewayCapacities;
};
```

## Shader Integration

### Thermal Visualization Shader
```glsl
// thermal.fs
#version 330

uniform sampler2D heatmapData;
uniform float maxTemp;
uniform float minTemp;

out vec4 finalColor;

void main() {
    float temp = texture(heatmapData, fragTexCoord).r;
    float normalized = (temp - minTemp) / (maxTemp - minTemp);
    
    // Blue -> Green -> Yellow -> Red gradient
    vec3 color;
    if (normalized < 0.25) {
        color = mix(vec3(0,0,1), vec3(0,1,0), normalized * 4.0);
    } else if (normalized < 0.5) {
        color = mix(vec3(0,1,0), vec3(1,1,0), (normalized - 0.25) * 4.0);
    } else {
        color = mix(vec3(1,1,0), vec3(1,0,0), (normalized - 0.5) * 2.0);
    }
    
    finalColor = vec4(color, 0.7);
}
```

### Airflow Visualization
```cpp
class AirflowVisualizer {
    ParticleSystem particles;
    RenderTexture2D flowField;
    
    void update(float dt) {
        // Update particle positions based on flow field
        particles.update(dt);
        
        // Render particles to texture
        BeginTextureMode(flowField);
        ClearBackground(BLANK);
        particles.render();
        EndTextureMode();
    }
};
```

## User Interface

### Tool System
```cpp
enum class Tool {
    SELECT,
    PLACE_RACK,
    PLACE_COOLING,
    PLACE_UTILITY,
    ZONE_PAINT,
    ROUTE_DUCT,
    MEASURE
};

class ToolManager {
    Tool currentTool = Tool::SELECT;
    ElementType placementType;
    
    void handleInput() {
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            switch (currentTool) {
                case Tool::PLACE_RACK:
                    tryPlaceElement();
                    break;
                case Tool::ZONE_PAINT:
                    paintZone();
                    break;
            }
        }
    }
};
```

### UI Rendering
```cpp
class UIRenderer {
    void renderToolbar() {
        // Top toolbar with tool buttons
        DrawRectangle(0, 0, screenWidth, 48, DARKGRAY);
        
        for (int i = 0; i < toolCount; i++) {
            Rectangle btn = {10 + i * 50, 8, 40, 32};
            DrawRectangleRec(btn, currentTool == i ? BLUE : GRAY);
            DrawTexture(toolIcons[i], btn.x + 4, btn.y + 4, WHITE);
        }
    }
    
    void renderInfoPanel() {
        // Right side panel with properties
        if (selectedElement) {
            DrawRectangle(screenWidth - 300, 48, 300, 400, LIGHTGRAY);
            DrawText(selectedElement->getName(), screenWidth - 290, 58, 20, BLACK);
            // Draw properties...
        }
    }
};
```

## Data Management

### Project File Format
```cpp
struct ProjectFile {
    struct Header {
        char magic[4] = {'D','C','L','E'};
        uint32_t version = 1;
        uint32_t roomWidth;
        uint32_t roomHeight;
    };
    
    struct ElementData {
        ElementType type;
        GridPos position;
        float rotation;
        Layer layer;
        map<string, variant<int, float, string>> properties;
    };
    
    void save(const string& filename) {
        // Binary format for efficiency
        ofstream file(filename, ios::binary);
        file.write((char*)&header, sizeof(header));
        
        // Write zones
        // Write elements
        // Write infrastructure
    }
};
```

### Undo/Redo System
```cpp
class CommandHistory {
    vector<unique_ptr<Command>> history;
    int currentIndex = -1;
    
    void execute(unique_ptr<Command> cmd) {
        cmd->execute();
        
        // Remove any commands after current
        history.erase(history.begin() + currentIndex + 1, history.end());
        
        history.push_back(move(cmd));
        currentIndex++;
    }
    
    void undo() {
        if (currentIndex >= 0) {
            history[currentIndex]->undo();
            currentIndex--;
        }
    }
};
```

## Performance Optimization

### Culling System
```cpp
class ViewCuller {
    Rectangle getViewBounds() {
        // Calculate visible area with margin
        return {
            camera.target.x - screenWidth/2 - margin,
            camera.target.y - screenHeight/2 - margin,
            screenWidth + margin*2,
            screenHeight + margin*2
        };
    }
    
    vector<ElementID> getVisibleElements() {
        Rectangle view = getViewBounds();
        return grid.getElementsInRect(view);
    }
};
```

### Spatial Indexing
```cpp
class SpatialIndex {
    struct Cell {
        vector<ElementID> elements;
    };
    
    static constexpr int CELL_SIZE = 10; // 10x10 tiles per cell
    array<array<Cell, GRID_HEIGHT/CELL_SIZE>, GRID_WIDTH/CELL_SIZE> cells;
    
    void insert(ElementID id, GridRect bounds) {
        // Add to all overlapping cells
    }
    
    vector<ElementID> query(Rectangle area) {
        // Return elements in area
    }
};
```

## Implementation Roadmap

### Phase 1: Foundation (Week 1-2)
- [ ] Grid system implementation
- [ ] Basic element placement
- [ ] Layer management
- [ ] Simple rendering pipeline

### Phase 2: Elements (Week 3-4)
- [ ] Rack placement with validation
- [ ] Zone painting system
- [ ] Basic UI tools
- [ ] Save/load functionality

### Phase 3: Infrastructure (Week 5-6)
- [ ] Underfloor duct routing
- [ ] Overhead raceway system
- [ ] Capacity tracking
- [ ] Collision detection

### Phase 4: Visualization (Week 7-8)
- [ ] Thermal map shader
- [ ] Airflow particles
- [ ] Utilization overlays
- [ ] Polish and optimization

### Phase 5: Advanced Features (Week 9-10)
- [ ] Power circuit tracking
- [ ] Network topology view
- [ ] Report generation
- [ ] Export capabilities

## Technical Considerations

### Platform Support
- Primary: Windows (native)
- Secondary: Linux (native)
- Future: macOS (if needed)

### Performance Targets
- 60 FPS with 1000+ elements
- <100ms save/load time
- <16MB RAM per floor

### Integration Points
- ResourceManager for asset loading
- Existing shader system
- CMake build system
- Cross-platform packaging