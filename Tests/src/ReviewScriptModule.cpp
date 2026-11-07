#include <Axle/ScriptableEntity.hpp>
#include <Axle/ScriptModule.hpp>
#include <Axle/Input.hpp>
#include <stdexcept>
#ifndef REVIEW_GENERATION
#define REVIEW_GENERATION 1
#endif
namespace {
    void (*sink)(int, int) = nullptr;
    class ReviewScript final : public Axle::ScriptableEntity {
    public:
        float Value = 1.0f;
        ~ReviewScript() override { if (sink) sink(2, REVIEW_GENERATION); }
        void OnDestroy() override { if (sink) sink(1, REVIEW_GENERATION); }
        void OnFixedUpdate(Axle::Timestep) override {
            if (Axle::Input::IsKeyPressed(Axle::KeyCode::W)) Value += 1.0f;
        }
    };
    Axle::ScriptableEntity* Create() {
#ifdef REVIEW_THROW_BIND
        throw std::runtime_error("Deliberate candidate bind failure");
#else
        return new ReviewScript;
#endif
    }
    void Destroy(Axle::ScriptableEntity* instance) { delete instance; }
}
extern "C" {
    AXLE_SCRIPT_API uint32_t Axle_GetScriptABIVersion() {
#ifdef REVIEW_BAD_ABI
        return 0;
#else
        return Axle::AXLE_SCRIPT_ABI_VERSION;
#endif
    }
    AXLE_SCRIPT_API uint64_t Axle_GetScriptBuildIdentity() {
#ifdef REVIEW_BAD_BUILD
        return Axle::AXLE_SCRIPT_BUILD_ID ^ (uint64_t(1) << 48);
#else
        return Axle::AXLE_SCRIPT_BUILD_ID;
#endif
    }
    AXLE_SCRIPT_API uint32_t Axle_GetScriptCount() { return 1; }
#ifndef REVIEW_BAD_EXPORTS
    AXLE_SCRIPT_API void Axle_InitializeScripts(Axle::ScriptRegisterCallback callback) {
        Axle::ScriptMetadata metadata;
        metadata.Name = "ReviewScript";
        metadata.Factory = Create;
        metadata.Destroyer = Destroy;
#ifdef REVIEW_BAD_METADATA
        metadata.Destroyer = nullptr;
#endif
        Axle::ScriptFieldRegistry fields;
        fields.AddField("Value", &ReviewScript::Value);
        metadata.Fields = fields.GetFields();
        callback("ReviewScript", &metadata);
    }
#endif
    AXLE_SCRIPT_API void Axle_ShutdownScripts() {}
    AXLE_SCRIPT_API void Axle_SetInputContext(void* context) { Axle::Input::SetInputContext(static_cast<const Axle::InputContext*>(context)); }
    AXLE_SCRIPT_API void AxleReview_SetSink(void (*callback)(int, int)) { sink = callback; }
}
