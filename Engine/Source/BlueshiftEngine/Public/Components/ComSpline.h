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

#pragma once

#include "Component.h"

BE_NAMESPACE_BEGIN

class ComTransform;

class ComSpline : public Component {
public:
    OBJECT_PROTOTYPE(ComSpline);

    ComSpline();
    virtual ~ComSpline();

    virtual void            Purge(bool chainPurge = true) override;

    virtual void            Init() override;

    virtual void            Awake() override;

    virtual void            DrawGizmos(const SceneView::Parms &sceneView, bool selected) override;

    float                   Length();

    Vec3                    GetCurrentOrigin(float time) const;
    Mat3                    GetCurrentAxis(float time) const;

protected:
    void                    UpdateCurve();
    void                    PropertyChanged(const char *classname, const char *propName);
    void                    PointTransformUpdated(const ComTransform *transform);

    Array<Guid>             pointGuids;
    Curve_Spline<Vec3> *    originCurve;
    Curve_Spline<Angles> *  anglesCurve;
    bool                    curveUpdated;
    bool                    loop;
};

BE_NAMESPACE_END
