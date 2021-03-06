// Copyright(c) 2017 POLYGONTEK
// 
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// 
// http ://www.apache.org/licenses/LICENSE-2.0
// 
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "Precompiled.h"
#include "Render/Render.h"
#include "Components/ComTransform.h"
#include "Components/ComRigidBody.h"
#include "Components/ComRenderable.h"
#include "Game/Entity.h"
#include "Game/GameWorld.h"

BE_NAMESPACE_BEGIN

ABSTRACT_DECLARATION("Renderable", ComRenderable, Component)
BEGIN_EVENTS(ComRenderable)
END_EVENTS
BEGIN_PROPERTIES(ComRenderable)
    PROPERTY_COLOR3("color", "Color", "", "1 1 1", PropertySpec::ReadWrite),
    PROPERTY_RANGED_FLOAT("alpha", "Alpha", "", Rangef(0, 1, 0.01f), "1", PropertySpec::ReadWrite),
    PROPERTY_BOOL("billboard", "Billboard", "", "false", PropertySpec::ReadWrite),
    PROPERTY_FLOAT("timeOffset", "Time Offset", "", "0", PropertySpec::ReadWrite),
    PROPERTY_FLOAT("timeScale", "Time Scale", "", "1", PropertySpec::ReadWrite),
    PROPERTY_FLOAT("maxVisDist", "Max Visible Distance", "max visible distance from viewer", "16384", PropertySpec::ReadWrite),
    PROPERTY_BOOL("skipSelection", "Skip Selection", "", "false", PropertySpec::ReadWrite | PropertySpec::Hidden | PropertySpec::SkipSerialization),
END_PROPERTIES

void ComRenderable::RegisterProperties() {
    //REGISTER_ACCESSOR_PROPERTY("Color", Color3, GetColor, SetColor, "1 1 1", PropertySpec::ReadWrite);
    //REGISTER_ACCESSOR_PROPERTY("Color", float, GetAlpha, SetAlpha, "1", PropertySpec::ReadWrite).SetRange(0, 1, 0.01f);
    //REGISTER_ACCESSOR_PROPERTY("Billboard", bool, IsBillboard, SetBillboard, "false", PropertySpec::ReadWrite);
    //REGISTER_ACCESSOR_PROPERTY("Time Offset", float, GetTimeOffset, SetTimeOffset, "0", PropertySpec::ReadWrite);
    //REGISTER_ACCESSOR_PROPERTY("Time Scale", float, GetTimeScale, SetTimeScale, "1", PropertySpec::ReadWrite);
    //REGISTER_ACCESSOR_PROPERTY("Max Visible Distance", float, GetMaxVisDist, SetMaxVisDist, "16384", PropertySpec::ReadWrite);
    //REGISTER_ACCESSOR_PROPERTY("Skip Selection", bool, IsSkipSelection, SetSkipSelection, "false", PropertySpec::ReadWrite | PropertySpec::Hidden | PropertySpec::SkipSerialization);
}

ComRenderable::ComRenderable() {
    sceneEntityHandle = -1;
    memset(&sceneEntity, 0, sizeof(sceneEntity));
    renderWorld = nullptr;
}

ComRenderable::~ComRenderable() {
    Purge(false);
}

void ComRenderable::Purge(bool chainPurge) {
    for (int i = 0; i < sceneEntity.customMaterials.Count(); i++) {
        materialManager.ReleaseMaterial(sceneEntity.customMaterials[i]);
    }
    sceneEntity.customMaterials.Clear();

    if (sceneEntityHandle != -1) {
        renderWorld->RemoveEntity(sceneEntityHandle);
        sceneEntityHandle = -1;
        renderWorld = nullptr;
    }

    if (chainPurge) {
        Component::Purge();
    }
}

void ComRenderable::Init() {
    Purge();

    Component::Init();

    renderWorld = GetGameWorld()->GetRenderWorld();

    memset(&sceneEntity, 0, sizeof(sceneEntity));	

    sceneEntity.layer = GetEntity()->GetLayer();
    sceneEntity.maxVisDist = props->Get("maxVisDist").As<float>();

    Color3 color = props->Get("color").As<Color3>();
    float alpha = props->Get("alpha").As<float>();
    sceneEntity.materialParms[SceneEntity::RedParm] = color.r;
    sceneEntity.materialParms[SceneEntity::GreenParm] = color.g;
    sceneEntity.materialParms[SceneEntity::BlueParm] = color.b;
    sceneEntity.materialParms[SceneEntity::AlphaParm] = alpha;
    sceneEntity.materialParms[SceneEntity::TimeOffsetParm] = props->Get("timeOffset").As<float>();
    sceneEntity.materialParms[SceneEntity::TimeScaleParm] = props->Get("timeScale").As<float>();
    sceneEntity.wireframeColor.Set(1, 1, 1, 1);

    sceneEntity.billboard = props->Get("billboard").As<bool>();

    sceneEntity.skipSelectionBuffer = props->Get("skipSelection").As<bool>();

    ComTransform *transform = GetEntity()->GetTransform();
    sceneEntity.origin = transform->GetOrigin();
    sceneEntity.scale = transform->GetScale();
    sceneEntity.axis = transform->GetAxis();

    GetEntity()->Connect(&SIG_LayerChanged, this, (SignalCallback)&ComRenderable::LayerChanged, SignalObject::Unique);

    transform->Connect(&SIG_TransformUpdated, this, (SignalCallback)&ComRenderable::TransformUpdated, SignalObject::Unique);
    transform->Connect(&SIG_PhysicsUpdated, this, (SignalCallback)&ComRenderable::PhysicsUpdated, SignalObject::Unique);
}

void ComRenderable::Enable(bool enable) {
    if (enable) {
        if (!IsEnabled()) {
            Component::Enable(true);
            UpdateVisuals();
        }
    } else {
        if (IsEnabled()) {
            Component::Enable(false);
            renderWorld->RemoveEntity(sceneEntityHandle);
            sceneEntityHandle = -1;
        }
    }
}

bool ComRenderable::HasRenderEntity(int sceneEntityHandle) const { 
    if (this->sceneEntityHandle == sceneEntityHandle) {
        return true;
    }

    return false;
}

void ComRenderable::UpdateVisuals() {
    if (!IsEnabled()) {
        return;
    }

    if (sceneEntityHandle == -1) {
        sceneEntityHandle = renderWorld->AddEntity(&sceneEntity);
    } else {
        renderWorld->UpdateEntity(sceneEntityHandle, &sceneEntity);
    }
}

const AABB ComRenderable::GetAABB() {
    const ComTransform *transform = GetEntity()->GetTransform();
    return sceneEntity.aabb * transform->GetScale();
}

bool ComRenderable::RayIntersection(const Vec3 &start, const Vec3 &dir, bool backFaceCull, float &lastScale) const {
    if (!sceneEntity.mesh) {
        return false;
    }

    Vec3 localStart = sceneEntity.axis.TransposedMulVec(start - sceneEntity.origin) / sceneEntity.scale;
    Vec3 localDir = sceneEntity.axis.TransposedMulVec(dir) / sceneEntity.scale;
    localDir.Normalize();

    float localS = sceneEntity.mesh->GetAABB().RayIntersection(localStart, localDir);
    if (localS == FLT_MAX) {
        return false;
    }

    float s = localS * (localDir * sceneEntity.scale).Length();
    if (s > lastScale) {
        return false;
    }

    if (sceneEntity.mesh->RayIntersection(localStart, localDir, backFaceCull, localS)) {
        s = localS * (localDir * sceneEntity.scale).Length();
        if (s > 0.0f && s < lastScale) {
            lastScale = s;
            return true;
        }
    }

    return false;
}

void ComRenderable::SetWireframeColor(const Color4 &color) {
    sceneEntity.wireframeColor = color;
    UpdateVisuals();
}

void ComRenderable::ShowWireframe(SceneEntity::WireframeMode wireframeMode) {
    sceneEntity.wireframeMode = wireframeMode;
    UpdateVisuals();
}

void ComRenderable::LayerChanged(const Entity *entity) {
    sceneEntity.layer = entity->props->Get("layer").As<int>();
    UpdateVisuals();
}

void ComRenderable::TransformUpdated(const ComTransform *transform) {
    sceneEntity.origin = transform->GetOrigin();
    sceneEntity.axis = transform->GetAxis();
    sceneEntity.scale = transform->GetScale();
    UpdateVisuals();
}

void ComRenderable::PhysicsUpdated(const PhysRigidBody *body) {
    sceneEntity.origin = body->GetOrigin();
    sceneEntity.axis = body->GetAxis();
    UpdateVisuals();
}

void ComRenderable::PropertyChanged(const char *classname, const char *propName) {
    if (!IsInitalized()) {
        return;
    }

    if (!Str::Cmp(propName, "maxVisDist")) {
        SetMaxVisDist(props->Get("maxVisDist").As<float>());
        return;
    } 
    
    if (!Str::Cmp(propName, "color")) {
        SetColor(props->Get("color").As<Color3>());
        return;
    } 
    
    if (!Str::Cmp(propName, "alpha")) {
        SetAlpha(props->Get("alpha").As<float>());
        return;
    } 
    
    if (!Str::Cmp(propName, "timeOffset")) {
        SetTimeOffset(props->Get("timeOffset").As<float>());
        return;
    } 
    
    if (!Str::Cmp(propName, "timeScale")) {
        SetTimeScale(props->Get("timeScale").As<float>());
        return;
    } 

    if (!Str::Cmp(propName, "billboard")) {
        SetBillboard(props->Get("billboard").As<bool>());
        return;
    } 

    if (!Str::Cmp(propName, "skipSelection")) {
        SetSkipSelection(props->Get("skipSelection").As<bool>());
        return;
    } 

    Component::PropertyChanged(classname, propName);
}

float ComRenderable::GetMaxVisDist() const {
    return sceneEntity.maxVisDist;
}

void ComRenderable::SetMaxVisDist(float maxVisDist) {
    sceneEntity.maxVisDist = maxVisDist;
    UpdateVisuals();
}

Color3 ComRenderable::GetColor() const {
    return Color3(&sceneEntity.materialParms[SceneEntity::RedParm]);
}

void ComRenderable::SetColor(const Color3 &color) {
    sceneEntity.materialParms[SceneEntity::RedParm] = color.r;
    sceneEntity.materialParms[SceneEntity::GreenParm] = color.g;
    sceneEntity.materialParms[SceneEntity::BlueParm] = color.b;
    UpdateVisuals();
}

float ComRenderable::GetAlpha() const {
    return sceneEntity.materialParms[SceneEntity::AlphaParm];
}

void ComRenderable::SetAlpha(float alpha) {
    sceneEntity.materialParms[SceneEntity::AlphaParm] = alpha;
    UpdateVisuals();
}

float ComRenderable::GetTimeOffset() const {
    return sceneEntity.materialParms[SceneEntity::TimeOffsetParm];
}

void ComRenderable::SetTimeOffset(float timeOffset) {
    sceneEntity.materialParms[SceneEntity::TimeOffsetParm] = timeOffset;
    UpdateVisuals();
}

float ComRenderable::GetTimeScale() const {
    return sceneEntity.materialParms[SceneEntity::TimeScaleParm];
}

void ComRenderable::SetTimeScale(float timeScale) {
    sceneEntity.materialParms[SceneEntity::TimeScaleParm] = timeScale;
    UpdateVisuals();
}

bool ComRenderable::IsBillboard() const {
    return sceneEntity.billboard;
}

void ComRenderable::SetBillboard(bool billboard) {
    sceneEntity.billboard = billboard;
    UpdateVisuals();
}

bool ComRenderable::IsSkipSelection() const {
    return sceneEntity.skipSelectionBuffer;
}

void ComRenderable::SetSkipSelection(bool skip) {
    sceneEntity.skipSelectionBuffer = skip;
    UpdateVisuals();
}

BE_NAMESPACE_END
