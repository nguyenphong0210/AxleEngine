#include <Axle/PluginSDK.hpp>
#include <AxleSpline/NativeSpline.hpp>
#include <memory>
#include <algorithm>
namespace {
using Data=AxleSpline::NativeSpline;
const Axle::SDK::RuntimeServices* services=nullptr;
std::uint64_t owner=0;
void* Create(void*,Axle::SDK::EntityRef,const char* bytes,std::uint32_t count) {
    try { auto value=std::make_unique<Data>(); if(count && (!bytes||!value->Read(std::string(bytes,count)))) return nullptr; return value.release(); }
    catch(...) { return nullptr; }
}
void Destroy(void*,void* value) { delete static_cast<Data*>(value); }
bool Serialize(void*,const void* value,Axle::SDK::TextSink sink,void* user) {
    try { if(!value||!sink) return false; auto text=static_cast<const Data*>(value)->Write(); return !text.empty()&&sink(user,text.data(),static_cast<std::uint32_t>(text.size())); }
    catch(...) { return false; }
}
void* Clone(void*,const void* value,Axle::SDK::EntityRef,const Axle::SDK::EntityRemap*,std::uint32_t) {
    try { if(!value) return nullptr; auto copy=std::make_unique<Data>(*static_cast<const Data*>(value)); copy->Running=false; return copy.release(); }
    catch(...) { return nullptr; }
}
void Start(void*,void* value,Axle::SDK::EntityRef) { static_cast<Data*>(value)->Running=true; }
void Stop(void*,void* value,Axle::SDK::EntityRef) { static_cast<Data*>(value)->Running=false; }
void Line(Axle::SDK::EntityRef entity,const Axle::SDK::Transform& t,const float* a,const float* b) {
    const auto transform=[&](const float* point,float* out) {
        const float v[]{point[0]*t.Scale[0],point[1]*t.Scale[1],point[2]*t.Scale[2]};
        const auto* q=t.Rotation;
        const float cross[]{2*(q[2]*v[2]-q[3]*v[1]),2*(q[3]*v[0]-q[1]*v[2]),2*(q[1]*v[1]-q[2]*v[0])};
        out[0]=t.Position[0]+v[0]+q[0]*cross[0]+q[2]*cross[2]-q[3]*cross[1];
        out[1]=t.Position[1]+v[1]+q[0]*cross[1]+q[3]*cross[0]-q[1]*cross[2];
        out[2]=t.Position[2]+v[2]+q[0]*cross[2]+q[1]*cross[1]-q[2]*cross[0];
    };
    float from[3],to[3]; transform(a,from); transform(b,to);
    const float color[]{0.2f,0.7f,1.f,1.f}; services->DebugLine(services->User,owner,entity.Scene,from,to,color);
}
void Update(void*,void* value,Axle::SDK::EntityRef entity,Axle::SDK::Phase phase,float) {
    if(!services||!value||phase!=Axle::SDK::Phase::Late) return;
    const auto& data=*static_cast<Data*>(value); if(!data.Running||!data.Valid()) return;
    Axle::SDK::Transform transform{}; if(!services->GetTransform(services->User,owner,entity,&transform)) return;
    for(std::size_t i=1;i<data.Points.size();++i) Line(entity,transform,data.Points[i-1].data(),data.Points[i].data());
}
}
AXLE_PLUGIN_EXPORT float* axle_spline_width(void* value) noexcept { return value?&static_cast<Data*>(value)->Width:nullptr; }
AXLE_PLUGIN_EXPORT float* axle_spline_step(void* value) noexcept { return value?&static_cast<Data*>(value)->Step:nullptr; }
#if defined(AXLE_PLUGIN_DESCRIPTOR) && defined(AXLE_PLUGIN_ID) && defined(AXLE_PLUGIN_MODULE_NAME)
AXLE_PLUGIN_DESCRIPTOR(AXLE_PLUGIN_ID,AXLE_PLUGIN_MODULE_NAME)
#endif
AXLE_PLUGIN_EXPORT bool ax_plugin_start(const Axle::PluginRuntime::PluginHostApi* host) noexcept {
    try {
        if(!host||host->struct_size!=sizeof(*host)||host->abi_version!=Axle::PluginRuntime::kPluginAbiVersion||!host->get_service) return false;
        services=static_cast<const Axle::SDK::RuntimeServices*>(host->get_service(host->user_data,Axle::SDK::RuntimeService,sizeof(Axle::SDK::RuntimeServices)));
        if(!services||services!=AxleRuntimeHost_GetV1()) return false;
        owner=host->owner_id; Axle::SDK::ComponentType type{};
        type.StructSize=sizeof(type); type.TypeId="axle.spline/Spline"; type.Schema=1; type.Required=true;
        type.Create=Create; type.Destroy=Destroy; type.Serialize=Serialize; type.Clone=Clone; type.Start=Start; type.Stop=Stop; type.Update=Update;
        return services->RegisterComponent(services->User,owner,&type);
    } catch(...) { return false; }
}
AXLE_PLUGIN_EXPORT void ax_plugin_stop() noexcept { services=nullptr; owner=0; }
