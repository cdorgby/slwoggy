#pragma once

#include <string>
#include <unordered_map>
#include <memory>
#include <vector>
#include <nlohmann/json.hpp>
#include "raylib.h"

// Forward declarations
struct lua_State;

// Resource entity represents a single resource
struct ResourceEntity {
    std::string entityId;    // Full ID: "org.mod.category.name"
    std::string version;     // "1.0.0"
    std::string modRoot;     // Which mod root this came from
};

// Mod catalog loaded from catalog.json
struct ModCatalog {
    std::string org;         // Organization name
    std::string mod;         // Mod name  
    std::string version;     // Mod version
    std::string rootPath;    // Absolute path to mod root
    std::vector<ResourceEntity> entities;
};

class ResourceManager {
public:
    static ResourceManager& getInstance();
    
    // Initialize with default assets path
    void initialize(const std::string& executablePath);
    
    // Add a mod root directory (scans catalog.json)
    bool addModRoot(const std::string& path);
    
    // Remove a mod root
    void removeModRoot(const std::string& path);
    
    // Get resource by entity ID
    const ResourceEntity* getEntity(const std::string& entityId) const;
    
    // Load resources by entity ID
    Texture2D loadTexture(const std::string& entityId);
    Font loadFont(const std::string& entityId, int fontSize = 32);
    Sound loadSound(const std::string& entityId);
    Music loadMusic(const std::string& entityId);
    Model loadModel(const std::string& entityId);
    Shader loadShader(const std::string& vertexId, const std::string& fragmentId);
    std::string loadScript(const std::string& entityId);  // Returns script content
    
    // Resolve module name to file path using org.mod convention
    std::string resolveModulePath(const std::string& moduleName) const;
    
    // Get all loaded catalogs
    const std::vector<ModCatalog>& getCatalogs() const { return m_catalogs; }
    
    // Unload all cached resources
    void unloadAll();
    
private:
    ResourceManager() = default;
    ~ResourceManager();
    
    ResourceManager(const ResourceManager&) = delete;
    ResourceManager& operator=(const ResourceManager&) = delete;
    
    // Scan a directory for catalog.json and load it
    bool scanModRoot(const std::string& path);
    
    // Load catalog.json from a path
    bool loadCatalog(const std::string& catalogPath, ModCatalog& catalog);
    
    // Build full entity ID from parts
    std::string buildEntityId(const std::string& org, const std::string& mod, 
                              const std::string& localId) const;
    
    // Get full file path for an entity (translates entity ID to file path)
    std::string getEntityPath(const std::string& entityId, const std::string& resourceType) const;
    
    // Platform-specific path detection
    void detectDefaultAssetPath(const std::string& executablePath);
    bool directoryExists(const std::string& path) const;
    
    // Storage
    std::vector<ModCatalog> m_catalogs;
    std::unordered_map<std::string, const ResourceEntity*> m_entityMap;  // entityId -> entity
    
    // Resource caches (using entity IDs as keys)
    std::unordered_map<std::string, Texture2D> m_textures;
    std::unordered_map<std::string, Font> m_fonts;
    std::unordered_map<std::string, Sound> m_sounds;
    std::unordered_map<std::string, Music> m_music;
    std::unordered_map<std::string, Model> m_models;
    std::unordered_map<std::string, Shader> m_shaders;
    std::unordered_map<std::string, std::string> m_scripts;  // entityId -> script content
    
    // Default paths
    std::string m_executablePath;
    std::string m_defaultAssetsPath;
    
    // Current context for relative module resolution
    mutable std::string m_currentOrg = "dorgby";
    mutable std::string m_currentMod = "dco";
};

