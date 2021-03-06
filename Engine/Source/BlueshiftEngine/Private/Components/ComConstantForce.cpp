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
#include "Components/ComTransform.h"
#include "Components/ComRigidBody.h"
#include "Components/ComConstantForce.h"
#include "Game/Entity.h"

BE_NAMESPACE_BEGIN

OBJECT_DECLARATION("Constant Force", ComConstantForce, Component)
BEGIN_EVENTS(ComConstantForce)
END_EVENTS
BEGIN_PROPERTIES(ComConstantForce)
    PROPERTY_VEC3("force", "Force", "", "0 0 0", PropertySpec::ReadWrite),
    PROPERTY_VEC3("torque", "Torque", "", "0 0 0", PropertySpec::ReadWrite),
END_PROPERTIES

void ComConstantForce::RegisterProperties() {
    //REGISTER_PROPERTY("Force", Vec3, force, "0 0 0", PropertySpec::ReadWrite);
    //REGISTER_PROPERTY("Torque", Vec3, torque, "0 0 0", PropertySpec::ReadWrite);
}

ComConstantForce::ComConstantForce() {
}

ComConstantForce::~ComConstantForce() {
}

void ComConstantForce::Init() {
    Purge();

    force = props->Get("force").As<Vec3>();
    torque = props->Get("torque").As<Vec3>();
}

void ComConstantForce::Update() {
    if (!IsEnabled()) {
        return;
    }

    const ComRigidBody *rigidBody = GetEntity()->GetComponent<ComRigidBody>();
    if (rigidBody) {
        rigidBody->GetBody()->ApplyCentralForce(force);
        rigidBody->GetBody()->ApplyTorque(torque);
    }
}

void ComConstantForce::PropertyChanged(const char *classname, const char *propName) {
    if (!IsInitalized()) {
        return;
    }

    if (!Str::Cmp(propName, "force")) {
        force = props->Get("force").As<Vec3>();
        return;
    }

     if (!Str::Cmp(propName, "torque")) {
        torque = props->Get("torque").As<Vec3>();
        return;
    }

    Component::PropertyChanged(classname, propName);
}

BE_NAMESPACE_END
