#pragma once

#include <string>
#include <memory>
#include <unordered_map>
#include <vector>

// Forward declarations
struct lua_State;

class ScriptContext {
public:
    ScriptContext(const std::string& scriptId, const std::string& filepath);
    ~ScriptContext();
    
    // No copying
    ScriptContext(const ScriptContext&) = delete;
    ScriptContext& operator=(const ScriptContext&) = delete;
    
    // Load and compile the script
    bool load(lua_State* L);
    
    // Execute the loaded script
    bool execute(lua_State* L);
    
    // Get script identity
    const std::string& getId() const { return m_scriptId; }
    const std::string& getFilepath() const { return m_filepath; }
    
    // Check if script is loaded
    bool isLoaded() const { return m_loaded; }
    
    // Store a reference to a Lua callback
    void setInitCallback(int ref) { m_initCallbackRef = ref; }
    int getInitCallback() const { return m_initCallbackRef; }
    
    // Event handlers - maps event name to list of callback refs
    void addEventHandler(const std::string& event, int ref);
    const std::vector<int>& getEventHandlers(const std::string& event) const;
    
private:
    std::string m_scriptId;      // Entity ID like "dorgby.dco.init"
    std::string m_filepath;      // Full path to script file
    bool m_loaded = false;       // Whether script is loaded
    
    // Lua references
    int m_initCallbackRef = -1;  // Reference to on_init callback
    std::unordered_map<std::string, std::vector<int>> m_eventHandlers;
    
    // TODO: Add support for pre-compiled bytecode
    // std::vector<uint8_t> m_bytecode;
};