#pragma once
#include <cstdint>
#include <array>
#include <functional>
#include <memory>
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace Axle {
class Entity;
class SceneData;
enum class ExtensionPhase { Frame, FixedControls, PostPhysics, Animation, Late };
struct ExtensionDebugLine {
    std::shared_ptr<void> CodeLease;
    std::array<float, 3> From, To;
    std::array<float, 4> Color;
};

// Host-owned implementations isolate optional systems from the data/simulation
// layers. Extension objects retain any executable code required by their data.
class SceneExtension {
public:
    virtual ~SceneExtension() = default;
    virtual void Clear() {}
    virtual void Destroy(Entity) = 0;
    virtual bool CanCopy(Entity) const = 0;
    virtual bool Copy(Entity destination, const SceneExtension& source, Entity sourceEntity,
                      const std::unordered_map<std::uint64_t, std::uint64_t>& remap) = 0;
    virtual bool Serialize(Entity, std::ostream&) const = 0;
    virtual bool Deserialize(Entity, const std::string&, const std::vector<std::string>&) = 0;
    virtual bool Ready(std::vector<std::string>& errors) const = 0;
    virtual void Start() {}
    virtual void Stop() {}
    virtual void Update(ExtensionPhase, float) {}
    virtual void ExtractDebug(std::vector<ExtensionDebugLine>&) const {}
    virtual void BeginTransaction() {}
    virtual bool EndTransaction(bool) { return true; }
};

class SceneExtensionRegistry {
public:
    using Factory = std::function<std::shared_ptr<SceneExtension>(SceneData&)>;
    static bool Register(const std::string& key, Factory factory);
    static void Unregister(const std::string& key);
};
}
