from pathlib import Path
import math,struct

root=Path('Editor/assets/materials/reference')
textures=root/'textures'
textures.mkdir(parents=True,exist_ok=True)
def tga(name,pixel,size=64):
    header=bytearray(18); header[2]=2
    struct.pack_into('<HH',header,12,size,size); header[16]=32; header[17]=0x28
    data=bytearray(header)
    for y in range(size):
        for x in range(size):
            r,g,b,a=pixel(x/size,y/size)
            data.extend((b,g,r,a))
    (textures/name).write_bytes(data)
def normal(x,y):
    a=0.22*math.sin(x*math.tau*4); b=0.22*math.cos(y*math.tau*4)
    return (round(127.5*(a+1)),round(127.5*(b+1)),round(255*math.sqrt(1-a*a-b*b)),255)
def weave(x,y):
    w=int(x*8)%2==int(y*8)%2
    v=int(45+35*(math.sin((x if w else y)*math.tau*32)*.5+.5))
    return(v,v,v,255)
tga('detail_normal.tga',normal)
tga('carbon_weave.tga',weave)
tga('coat.tga',lambda x,y:(180+int(75*x),230,0,255))
tga('flow.tga',lambda x,y:(255,128,0,255))
tga('detail_mask.tga',lambda x,y:(255 if (int(x*4)+int(y*4))%2 else 64,0,0,255))
tga('background.tga',lambda x,y:((230,70,25,255) if (int(x*8)+int(y*8))%2 else (25,100,220,255)))
base={'shader':'"ForwardPBR"','useCustomShader':'false','surfaceType':'"Opaque"','cullMode':'"Back"','depthWrite':'true','albedoColor':'[0.5,0.5,0.5,1]','metallic':'0','roughness':'0.5','normalStrength':'1','uvTiling':'[1,1]','uvOffset':'[0,0]'}
prefix='materials/reference/textures/'
materials={
 'CarPaint':{'albedoColor':'[0.45,0.015,0.025,1]','roughness':'0.25','clearcoatStrength':'1','clearcoatRoughness':'0.08','clearcoatMap':f'"{prefix}coat.tga"','useClearcoatMap':'true'},
 'BrushedMetal':{'albedoColor':'[0.75,0.78,0.82,1]','metallic':'1','roughness':'0.32','anisotropyStrength':'0.85','anisotropyFlowMap':f'"{prefix}flow.tga"','useAnisotropyFlowMap':'true'},
 'CarbonFiber':{'albedoColor':'[0.3,0.3,0.3,1]','albedoMap':f'"{prefix}carbon_weave.tga"','metallic':'0.6','roughness':'0.35','clearcoatStrength':'1','clearcoatRoughness':'0.1','anisotropyStrength':'0.55','detailNormalMap':f'"{prefix}detail_normal.tga"','useDetailNormalMap':'true','detailNormalStrength':'0.35','detailUVScale':'[4,4]'},
 'Rubber':{'albedoColor':'[0.025,0.028,0.032,1]','roughness':'0.8','detailNormalMap':f'"{prefix}detail_normal.tga"','useDetailNormalMap':'true','detailNormalStrength':'1.2','detailUVScale':'[10,10]'},
 'Leather':{'albedoColor':'[0.15,0.045,0.018,1]','roughness':'0.65','detailNormalMap':f'"{prefix}detail_normal.tga"','useDetailNormalMap':'true','detailMaskMap':f'"{prefix}detail_mask.tga"','useDetailMaskMap':'true','detailNormalStrength':'0.8','detailUVScale':'[7,7]'},
 'Glass':{'albedoColor':'[0.9,0.97,1,0.15]','roughness':'0.06','surfaceType':'"Transparent"','depthWrite':'false','refractionEnabled':'true','refractionStrength':'1','refractionIOR':'1.5','refractionThickness':'0.4','refractionTint':'[0.72,0.92,0.98]','renderQueue':'3000'},
 'Backdrop':{'albedoColor':'[1,1,1,1]','albedoMap':f'"{prefix}background.tga"','roughness':'1','specularHighlights':'false','environmentReflections':'false'},
}
for name,overrides in materials.items():
    properties={'name':f'"Reference_{name}"',**base,**overrides}
    (root/(name+'.axleasset')).write_text('Material {\n'+''.join(f'    {key}: {value}\n' for key,value in properties.items())+'}\n',encoding='utf-8')

lines=['# Phase 38.3 material acceptance scene','Scene: MaterialReference','Entities:']
def entity(index,name,translation,scale=(1,1,1),mesh=None,material=None):
    lines.extend([f'  - Entity: {383000+index}','    TagComponent:',f'      Tag: {name}','    TransformComponent:',f'      Translation: [ {" , ".join(map(str,translation))} ]','      Rotation: [ 1 , 0 , 0 , 0 ]',f'      Scale: [ {" , ".join(map(str,scale))} ]'])
    if mesh: lines.extend(['    MeshInstance3D:',f'      AssetPath: builtin://{mesh}','      PatternMode: 3'])
    if material: lines.extend(['    MaterialComponent:',f'      AssetPath: materials/reference/{material}.axleasset'])
for index,name in enumerate(['CarPaint','BrushedMetal','CarbonFiber','Rubber','Leather','Glass']):
    x=(index%3-1)*2.6; y=1.4 if index<3 else -1.4
    entity(index+1,'Reference_'+name,(x,y,0),(.95,.95,.95),'sphere',name)
entity(10,'Reference_Backdrop',(0,0,-1.4),(10,6,.1),'cube','Backdrop')
entity(11,'Reference_Camera',(0,0,10))
lines.extend(['    CameraComponent:','      Primary: true','      FOV: 45','      NearClip: 0.1','      FarClip: 100'])
entity(12,'Reference_KeyLight',(0,0,0))
lines.extend(['    DirectionalLightComponent:','      Color: [ 1 , 0.97 , 0.92 ]','      Intensity: 1.2','      AmbientIntensity: 0.25','      Azimuth: 35','      Elevation: 50','      CastShadows: false'])
entity(13,'Reference_FillLight',(-4,3,3))
lines.extend(['    PointLightComponent:','      Color: [ 0.7 , 0.8 , 1 ]','      Intensity: 80','      Radius: 15'])
Path('Editor/assets/scenes/MaterialReference.axlescene').write_text('\n'.join(lines)+'\n',encoding='utf-8')
