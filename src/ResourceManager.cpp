#include "ResourceManager.h"
#include "raylib.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

namespace fs = std::filesystem;
using json = nlohmann::json;

ResourceManager& ResourceManager::getInstance() {
    static ResourceManager instance;
    return instance;
}

void ResourceManager::initialize(const std::string& executablePath) {
    m_executablePath = executablePath;
    detectDefaultAssetPath(executablePath);
    
    // Scan default assets with default org/mod
    if (!m_defaultAssetsPath.empty()) {
        std::cout << "ResourceManager2 initialized with default assets: " << m_defaultAssetsPath << std::endl;
        
        // Create default catalog if none exists
        std::string catalogPath = m_defaultAssetsPath + "/catalog.json";
        if (!fs::exists(catalogPath)) {
            // Create default catalog
            json defaultCatalog;
            defaultCatalog["org"] = "dorgby";
            defaultCatalog["mod"] = "dco";
            defaultCatalog["version"] = "1.0.0";
            
            // Add init.lua as the only initial entity
            json initEntity;
            initEntity["id"] = "init";
            initEntity["type"] = "script";
            initEntity["path"] = "scripts/init.lua";
            defaultCatalog["entities"] = json::array({initEntity});
            
            // Write default catalog
            std::ofstream out(catalogPath);
            if (out.is_open()) {
                out << defaultCatalog.dump(2);
                out.close();
                std::cout << "Created default catalog.json" << std::endl;
            }
        }
        
        // Add default assets as mod root
        addModRoot(m_defaultAssetsPath);
    }
}

bool ResourceManager::addModRoot(const std::string& path) {
    if (!directoryExists(path)) {
        std::cerr << "Mod root does not exist: " << path << std::endl;
        return false;
    }
    
    // Check if already loaded
    for (const auto& catalog : m_catalogs) {
        if (catalog.rootPath == path) {
            std::cout << "Mod root already loaded: " << path << std::endl;
            return true;
        }
    }
    
    return scanModRoot(path);
}

void ResourceManager::removeModRoot(const std::string& path) {
    auto it = std::remove_if(m_catalogs.begin(), m_catalogs.end(),
        [&path](const ModCatalog& catalog) {
            return catalog.rootPath == path;
        });
    
    if (it != m_catalogs.end()) {
        // Remove entities from map
        for (const auto& entity : it->entities) {
            m_entityMap.erase(entity.entityId);
        }
        
        m_catalogs.erase(it, m_catalogs.end());
        std::cout << "Removed mod root: " << path << std::endl;
    }
}

bool ResourceManager::scanModRoot(const std::string& path) {
    std::string catalogPath = path + "/catalog.json";
    
    if (!fs::exists(catalogPath)) {
        std::cerr << "No catalog.json found in: " << path << std::endl;
        return false;
    }
    
    ModCatalog catalog;
    // Keep relative paths when possible to avoid WSL issues
    if (fs::path(path).is_relative()) {
        catalog.rootPath = path;
    } else {
        catalog.rootPath = fs::absolute(path).string();
    }
    
    if (!loadCatalog(catalogPath, catalog)) {
        return false;
    }
    
    // Add to catalogs
    m_catalogs.push_back(catalog);
    
    // Update entity map
    for (const auto& entity : catalog.entities) {
        m_entityMap[entity.entityId] = &m_catalogs.back().entities[&entity - &catalog.entities[0]];
    }
    
    std::cout << "Loaded mod: " << catalog.org << "." << catalog.mod 
              << " v" << catalog.version << " from " << path << std::endl;
    std::cout << "  Entities: " << catalog.entities.size() << std::endl;
    
    return true;
}

bool ResourceManager::loadCatalog(const std::string& catalogPath, ModCatalog& catalog) {
    try {
        std::ifstream file(catalogPath);
        if (!file.is_open()) {
            std::cerr << "Failed to open catalog: " << catalogPath << std::endl;
            return false;
        }
        
        json j;
        file >> j;
        
        // Parse basic info
        catalog.org = j.value("org", "unknown");
        catalog.mod = j.value("mod", "unknown");
        catalog.version = j.value("version", "1.0.0");
        
        // Parse entities
        if (j.contains("entities") && j["entities"].is_array()) {
            for (const auto& entityJson : j["entities"]) {
                ResourceEntity entity;
                
                // Build full entity ID
                std::string localId = entityJson.value("id", "");
                if (localId.empty()) continue;
                
                entity.entityId = buildEntityId(catalog.org, catalog.mod, localId);
                entity.version = entityJson.value("version", catalog.version);
                entity.modRoot = catalog.rootPath;
                
                catalog.entities.push_back(entity);
            }
        }
        
        return true;
        
    } catch (const json::exception& e) {
        std::cerr << "Error parsing catalog.json: " << e.what() << std::endl;
        return false;
    }
}

std::string ResourceManager::buildEntityId(const std::string& org, const std::string& mod, 
                                           const std::string& localId) const {
    return org + "." + mod + "." + localId;
}

const ResourceEntity* ResourceManager::getEntity(const std::string& entityId) const {
    auto it = m_entityMap.find(entityId);
    return (it != m_entityMap.end()) ? it->second : nullptr;
}

std::string ResourceManager::getEntityPath(const std::string& entityId, const std::string& resourceType) const {
    const ResourceEntity* entity = getEntity(entityId);
    if (!entity) {
        return "";
    }
    
    // Extract the local part after org.mod.
    std::string localPart = entityId;
    size_t secondDot = entityId.find('.', entityId.find('.') + 1);
    if (secondDot != std::string::npos) {
        localPart = entityId.substr(secondDot + 1);
    }
    
    // Replace dots with slashes for path
    std::string relativePath = localPart;
    std::replace(relativePath.begin(), relativePath.end(), '.', '/');
    
    // Add resource type directory and extension
    std::string fullRelativePath;
    if (resourceType == "script") {
        fullRelativePath = "scripts/" + relativePath + ".lua";
    } else if (resourceType == "texture") {
        fullRelativePath = "graphics/" + relativePath + ".png";
    } else if (resourceType == "shader_vertex") {
        fullRelativePath = "shaders/" + relativePath + ".vs";
    } else if (resourceType == "shader_fragment") {
        fullRelativePath = "shaders/" + relativePath + ".fs";
    } else if (resourceType == "sound") {
        fullRelativePath = "sounds/" + relativePath + ".wav";
    } else if (resourceType == "music") {
        fullRelativePath = "music/" + relativePath + ".mp3";
    } else if (resourceType == "font") {
        fullRelativePath = "fonts/" + relativePath + ".ttf";
    } else if (resourceType == "model") {
        fullRelativePath = "models/" + relativePath + ".obj";
    } else {
        return ""; // Unknown resource type
    }
    
    fs::path fullPath = fs::path(entity->modRoot) / fullRelativePath;
    
    // Normalize the path to avoid double prefixes
    std::string pathStr = fullPath.string();
    std::replace(pathStr.begin(), pathStr.end(), '\\', '/');
    
    // Check for double WSL prefix
    size_t wslPos = pathStr.find("//wsl");
    if (wslPos != std::string::npos) {
        size_t secondWsl = pathStr.find("wsl$", wslPos + 5);
        if (secondWsl != std::string::npos) {
            // Remove the duplicate part
            pathStr = pathStr.substr(0, wslPos) + pathStr.substr(secondWsl - 1);
        }
    }
    
    return pathStr;
}

Texture2D ResourceManager::loadTexture(const std::string& entityId) {
    // Check cache first
    auto it = m_textures.find(entityId);
    if (it != m_textures.end()) {
        return it->second;
    }
    
    // Find entity
    const ResourceEntity* entity = getEntity(entityId);
    if (!entity) {
        // Entity not found - return empty texture
        return {0};
    }
    
    // Load texture
    std::string fullPath = getEntityPath(entityId, "texture");
    Texture2D texture = LoadTexture(fullPath.c_str());
    
    if (texture.id != 0) {
        m_textures[entityId] = texture;
        std::cout << "Loaded texture: " << entityId << " from " << fullPath << std::endl;
    } else {
        std::cerr << "Failed to load texture: " << fullPath << std::endl;
    }
    
    return texture;
}

Font ResourceManager::loadFont(const std::string& entityId, int fontSize) {
    // Create cache key with size
    std::string cacheKey = entityId + "_" + std::to_string(fontSize);
    
    // Check cache first
    auto it = m_fonts.find(cacheKey);
    if (it != m_fonts.end()) {
        return it->second;
    }
    
    // Find entity
    const ResourceEntity* entity = getEntity(entityId);
    if (!entity) {
        // Entity not found - return default font
        return GetFontDefault();
    }
    
    // Load font
    std::string fullPath = getEntityPath(entityId, "font");
    Font font = LoadFontEx(fullPath.c_str(), fontSize, nullptr, 0);
    
    if (font.texture.id != 0) {
        m_fonts[cacheKey] = font;
        std::cout << "Loaded font: " << entityId << " size " << fontSize << std::endl;
    } else {
        std::cerr << "Failed to load font: " << fullPath << std::endl;
        return GetFontDefault();
    }
    
    return font;
}

Sound ResourceManager::loadSound(const std::string& entityId) {
    // Check cache first
    auto it = m_sounds.find(entityId);
    if (it != m_sounds.end()) {
        return it->second;
    }
    
    // Find entity
    const ResourceEntity* entity = getEntity(entityId);
    if (!entity) {
        // Entity not found - return empty sound
        return {0};
    }
    
    // Load sound
    std::string fullPath = getEntityPath(entityId, "sound");
    Sound sound = LoadSound(fullPath.c_str());
    
    if (sound.frameCount > 0) {
        m_sounds[entityId] = sound;
        std::cout << "Loaded sound: " << entityId << std::endl;
    } else {
        std::cerr << "Failed to load sound: " << fullPath << std::endl;
    }
    
    return sound;
}

Music ResourceManager::loadMusic(const std::string& entityId) {
    // Check cache first
    auto it = m_music.find(entityId);
    if (it != m_music.end()) {
        return it->second;
    }
    
    // Find entity
    const ResourceEntity* entity = getEntity(entityId);
    if (!entity) {
        // Entity not found - return empty music
        return {0};
    }
    
    // Load music
    std::string fullPath = getEntityPath(entityId, "music");
    Music music = LoadMusicStream(fullPath.c_str());
    
    if (music.frameCount > 0) {
        m_music[entityId] = music;
        std::cout << "Loaded music: " << entityId << std::endl;
    } else {
        std::cerr << "Failed to load music: " << fullPath << std::endl;
    }
    
    return music;
}

Model ResourceManager::loadModel(const std::string& entityId) {
    // Check cache first
    auto it = m_models.find(entityId);
    if (it != m_models.end()) {
        return it->second;
    }
    
    // Find entity
    const ResourceEntity* entity = getEntity(entityId);
    if (!entity) {
        // Entity not found - return empty model
        return {0};
    }
    
    // Load model
    std::string fullPath = getEntityPath(entityId, "model");
    Model model = LoadModel(fullPath.c_str());
    
    if (model.meshCount > 0) {
        m_models[entityId] = model;
        std::cout << "Loaded model: " << entityId << std::endl;
    } else {
        std::cerr << "Failed to load model: " << fullPath << std::endl;
    }
    
    return model;
}

Shader ResourceManager::loadShader(const std::string& vertexId, const std::string& fragmentId) {
    // Create cache key
    std::string cacheKey = vertexId + "+" + fragmentId;
    
    // Check cache first
    auto it = m_shaders.find(cacheKey);
    if (it != m_shaders.end()) {
        return it->second;
    }
    
    // Load vertex shader path
    const char* vsPath = nullptr;
    std::string vsFullPath;
    if (!vertexId.empty()) {
        const ResourceEntity* vsEntity = getEntity(vertexId);
        if (!vsEntity) {
            // Vertex shader entity not found
            return {0};
        }
        vsFullPath = getEntityPath(vertexId, "shader_vertex");
        vsPath = vsFullPath.c_str();
    }
    
    // Load fragment shader path
    const char* fsPath = nullptr;
    std::string fsFullPath;
    if (!fragmentId.empty()) {
        const ResourceEntity* fsEntity = getEntity(fragmentId);
        if (!fsEntity) {
            // Fragment shader entity not found
            return {0};
        }
        fsFullPath = getEntityPath(fragmentId, "shader_fragment");
        fsPath = fsFullPath.c_str();
    }
    
    // Load shader
    Shader shader = LoadShader(vsPath, fsPath);
    
    if (shader.id > 0) {
        m_shaders[cacheKey] = shader;
        std::cout << "Loaded shader: " << vertexId << " + " << fragmentId << std::endl;
    } else {
        std::cerr << "Failed to load shader" << std::endl;
    }
    
    return shader;
}

std::string ResourceManager::loadScript(const std::string& entityId) {
    // Check cache first
    auto it = m_scripts.find(entityId);
    if (it != m_scripts.end()) {
        return it->second;
    }
    
    // Get entity
    const ResourceEntity* entity = getEntity(entityId);
    if (!entity) {
        // Don't print error - let caller decide if this is an error
        return "";
    }
    
    // Get script path
    std::string fullPath = getEntityPath(entityId, "script");
    if (fullPath.empty()) {
        std::cerr << "Could not resolve script path for: " << entityId << std::endl;
        return "";
    }
    
    // Read script content
    std::ifstream file(fullPath);
    if (!file.is_open()) {
        std::cerr << "Failed to open script file: " << fullPath << std::endl;
        return "";
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    
    // Cache the content
    m_scripts[entityId] = content;
    
    std::cout << "Loaded script: " << entityId << " from " << fullPath << std::endl;
    return content;
}

std::string ResourceManager::resolveModulePath(const std::string& moduleName) const {
    // Validate module name - no extensions, no paths
    if (moduleName.empty() || 
        moduleName.find('/') != std::string::npos ||
        moduleName.find('\\') != std::string::npos ||
        moduleName.find("..") != std::string::npos) {
        return "";
    }
    
    // Split module name by dots
    std::vector<std::string> parts;
    std::string current;
    for (char c : moduleName) {
        if (c == '.') {
            if (!current.empty()) {
                parts.push_back(current);
                current.clear();
            }
        } else {
            current += c;
        }
    }
    if (!current.empty()) {
        parts.push_back(current);
    }
    
    if (parts.empty()) {
        return "";
    }
    
    std::string org, mod;
    std::vector<std::string> pathParts;
    
    // Check if it's a full path (org.mod.path...)
    if (parts.size() >= 3) {
        // Check if first two parts match any loaded catalog
        bool isFullPath = false;
        for (const auto& catalog : m_catalogs) {
            if (catalog.org == parts[0] && catalog.mod == parts[1]) {
                isFullPath = true;
                break;
            }
        }
        
        if (isFullPath) {
            org = parts[0];
            mod = parts[1];
            pathParts.assign(parts.begin() + 2, parts.end());
        } else {
            // Relative path
            org = m_currentOrg;
            mod = m_currentMod;
            pathParts = parts;
        }
    } else {
        // Relative path
        org = m_currentOrg;
        mod = m_currentMod;
        pathParts = parts;
    }
    
    // Find the catalog for this org.mod
    std::string rootPath;
    for (const auto& catalog : m_catalogs) {
        if (catalog.org == org && catalog.mod == mod) {
            rootPath = catalog.rootPath;
            break;
        }
    }
    
    if (rootPath.empty()) {
        return "";
    }
    
    // Build the file path
    std::string filePath = rootPath + "/scripts";
    for (const auto& part : pathParts) {
        filePath += "/" + part;
    }
    filePath += ".lua";
    
    // Verify the file exists
    if (!fs::exists(filePath)) {
        return "";
    }
    
    return filePath;
}

void ResourceManager::detectDefaultAssetPath(const std::string& executablePath) {
    // Get the directory containing the executable
    fs::path exePath(executablePath);
    fs::path exeDir = exePath.parent_path();
    
    // Common relative paths to check - use relative paths to avoid WSL issues
    std::vector<fs::path> possiblePaths = {
        "../assets",                       // build/platform/bin/../assets (relative)
        "assets",                          // bin/assets (relative)
        exeDir / ".." / "assets",          // build/platform/bin/../assets (absolute)
        exeDir / "assets",                 // bin/assets (absolute)
        fs::current_path() / "assets",     // cwd/assets (absolute)
    };
    
    // Find first existing path
    for (const auto& path : possiblePaths) {
        if (directoryExists(path.string())) {
            // For Windows running from WSL, prefer relative paths
            if (path.is_relative()) {
                m_defaultAssetsPath = path.string();
            } else {
                m_defaultAssetsPath = fs::absolute(path).string();
            }
            std::replace(m_defaultAssetsPath.begin(), m_defaultAssetsPath.end(), '\\', '/');
            break;
        }
    }
}

bool ResourceManager::directoryExists(const std::string& path) const {
    return fs::exists(path) && fs::is_directory(path);
}

void ResourceManager::unloadAll() {
    // Unload textures
    for (auto& pair : m_textures) {
        UnloadTexture(pair.second);
    }
    m_textures.clear();
    
    // Unload fonts
    for (auto& pair : m_fonts) {
        UnloadFont(pair.second);
    }
    m_fonts.clear();
    
    // Unload sounds
    for (auto& pair : m_sounds) {
        UnloadSound(pair.second);
    }
    m_sounds.clear();
    
    // Unload music
    for (auto& pair : m_music) {
        UnloadMusicStream(pair.second);
    }
    m_music.clear();
    
    // Unload models
    for (auto& pair : m_models) {
        UnloadModel(pair.second);
    }
    m_models.clear();
    
    // Unload shaders
    for (auto& pair : m_shaders) {
        UnloadShader(pair.second);
    }
    m_shaders.clear();
}

ResourceManager::~ResourceManager() {
    unloadAll();
}