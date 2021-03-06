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
#include "RenderInternal.h"
#include "Platform/PlatformTime.h"

BE_NAMESPACE_BEGIN

static const int HOM_CULL_TEXTURE_WIDTH = 4096;
static const int HOM_CULL_TEXTURE_HEIGHT = 1;

BackEnd	backEnd;

static void RB_InitStencilStates() {
    backEnd.stencilStates[BackEnd::VolumeIntersectionZPass] = 
        glr.CreateStencilState(~0, ~0, 
        Renderer::AlwaysFunc, Renderer::KeepOp, Renderer::KeepOp, Renderer::DecrWrapOp, 
        Renderer::AlwaysFunc, Renderer::KeepOp, Renderer::KeepOp, Renderer::IncrWrapOp);

    backEnd.stencilStates[BackEnd::VolumeIntersectionZFail] = 
        glr.CreateStencilState(~0, ~0, 
        Renderer::AlwaysFunc, Renderer::KeepOp, Renderer::IncrWrapOp, Renderer::KeepOp, 
        Renderer::AlwaysFunc, Renderer::KeepOp, Renderer::DecrWrapOp, Renderer::KeepOp);

    backEnd.stencilStates[BackEnd::VolumeIntersectionInsideZFail] = 
        glr.CreateStencilState(~0, ~0, 
        Renderer::AlwaysFunc, Renderer::KeepOp, Renderer::IncrWrapOp, Renderer::KeepOp, 
        Renderer::NeverFunc, Renderer::KeepOp, Renderer::KeepOp, Renderer::KeepOp);

    backEnd.stencilStates[BackEnd::VolumeIntersectionTest] = 
        glr.CreateStencilState(~0, 0, 
        Renderer::EqualFunc, Renderer::KeepOp, Renderer::KeepOp, Renderer::KeepOp, 
        Renderer::NeverFunc, Renderer::KeepOp, Renderer::KeepOp, Renderer::KeepOp);
}

static void RB_FreeStencilStates() {
    for (int i = 0; i < BackEnd::MaxPredefinedStencilStates; i++) {
        glr.DeleteStencilState(backEnd.stencilStates[i]);
    }
}

static void RB_InitLightQueries() {
    /*for (int i = 0; i < MAX_LIGHTS; i++) {
        backEnd.lightQueries[i].queryHandle = glr.CreateQuery();
        backEnd.lightQueries[i].light = nullptr;
        backEnd.lightQueries[i].frameCount = 0;
        backEnd.lightQueries[i].resultSamples = 0;
    }*/	
}

static void RB_FreeLightQueries() {
    /*for (int i = 0; i < MAX_LIGHTS; i++) {
        glr.DeleteQuery(backEnd.lightQueries[i].queryHandle);
    }*/
}

void RB_Init() {	
    backEnd.rbsurf.Init();

    RB_InitStencilStates();
    
    RB_InitLightQueries();

    PP_Init();

    memset(backEnd.csmUpdate, 0, sizeof(backEnd.csmUpdate));

    // FIXME: TEMPORARY CODE
    backEnd.irradianceCubeMapTexture = textureManager.AllocTexture("irradianceCubeMap");
    backEnd.irradianceCubeMapTexture->Load("env/irradiance/test_irr", Texture::Clamp | Texture::HighQuality | Texture::CubeMap | Texture::SRGB);

    if (r_HOM.GetBool()) {
        // TODO: create one for each context
        backEnd.homCullingOutputTexture = textureManager.AllocTexture("_homCullingOutput");
        backEnd.homCullingOutputTexture->CreateEmpty(Renderer::Texture2D, HOM_CULL_TEXTURE_WIDTH, HOM_CULL_TEXTURE_HEIGHT, 1, 1,
            Image::RGBA_8_8_8_8, Texture::Clamp | Texture::Nearest | Texture::NoMipmaps | Texture::HighQuality);
        backEnd.homCullingOutputRT = RenderTarget::Create(backEnd.homCullingOutputTexture, nullptr, 0);
    }

    backEnd.initialized = true;
}

void RB_Shutdown() {
    backEnd.initialized = false;

    //Texture::DestroyInstanceImmediate(backEnd.irradianceCubeMapTexture);

    PP_Free();

    RB_FreeLightQueries();

    RB_FreeStencilStates();

    backEnd.rbsurf.Shutdown();
}

static const void *RB_ExecuteBeginContext(const void *data) {
    BeginContextRenderCommand *cmd = (BeginContextRenderCommand *)data;	

    backEnd.ctx = cmd->renderContext;

    return (const void *)(cmd + 1);
}

void RB_DrawLightVolume(const SceneLight *light) {
    switch (light->parms.type) {
    case SceneLight::DirectionalLight:
        RB_DrawOBB(light->GetOBB());
        break;
    case SceneLight::PointLight:
        if (light->IsRadiusUniform()) {
            RB_DrawSphere(Sphere(light->GetOrigin(), light->GetRadius()[0]), 16, 16);
        } else {
            // FIXME: ellipsoid 그리기로 바꾸자
            RB_DrawOBB(light->GetOBB());
        }
        break;
    case SceneLight::SpotLight:
        RB_DrawFrustum(light->GetFrustum());
        break;
    default:
        break;
    }
}

static void RB_DrawStencilLightVolume(const viewLight_t *light, bool insideLightVolume) {
    glr.SetStateBits(Renderer::DF_LEqual);

    if (insideLightVolume) {
        glr.SetCullFace(Renderer::NoCull);
        glr.SetStencilState(backEnd.stencilStates[BackEnd::VolumeIntersectionInsideZFail], 0);
    } else {
        glr.SetCullFace(Renderer::NoCull);
        glr.SetStencilState(backEnd.stencilStates[BackEnd::VolumeIntersectionZPass], 0);		
    }

    RB_DrawLightVolume(light->def);

    glr.SetStencilState(Renderer::NullStencilState, 0);
}

// NOTE: ambient pass 이후에 실행되므로 화면에 깊이값은 채워져있다
static void RB_MarkOcclusionVisibleLights(int numLights, viewLight_t **lights) {
/*	bool    insideLightVolume;
    Rect    prevScissorRect;
    int     numQueryWait = 0;
    int     numQueryResult = 0;
    int     numVisLights = 0;

    if (r_useLightScissors.GetBool()) {
        prevScissorRect = glr.GetScissor();
    }	

    for (int i = 0; i < numLights; i++) {
        viewLight_t *light = lights[i];
        if (light->def->parms.isMainLight) {
            continue;
        }

        LightQuery *lightQuery = &backEnd.lightQueries[light->index];
            
        // light 가 query 중이었다면..
        if (lightQuery->light) {
            // 아직 query result 를 사용할 수 없고, query wait frame 한도를 넘지 않았다면..
            if (!glr.QueryResultAvailable(lightQuery->queryHandle) && renderConfig.frameCount - lightQuery->frameCount < r_queryWaitFrames.GetInteger()) {
                // 이전 result sample 로 visibility 를 판단한다
                if (lightQuery->resultSamples >= 10) {
                    light->occlusionVisible = true;
                    numVisLights++;
                } else {
                    //BE_LOG(L"%i\n", lightQuery->resultSamples);
                }

                numQueryWait++;
                continue;
            }

            // query result 를 가져온다. unavailable 일 경우 blocking 상태가 된다.
            lightQuery->resultSamples = glr.QueryResult(lightQuery->queryHandle);
            numQueryResult++;
        } else {
            lightQuery->light = light;
            lightQuery->resultSamples = 10; // visible threshold 값
        }

        if (lightQuery->resultSamples >= 10) {
            light->occlusionVisible = true;
            numVisLights++;
        }
        
        lightQuery->frameCount = renderConfig.frameCount;

        glr.Clear(ClearStencil, Vec4::zero, 0.0f, 0);

        if (r_useLightScissors.GetBool()) {
            glr.SetScissor(light->scissorRect);
        }

        // 카메라가 light volume 안에 있는지 체크
        if (light->def->parms.type == LT_OMNI) {
            if (light->def->IsRadiusUniform()) {
                float radius = light->def->GetRadius().x;
                insideLightVolume = light->def->parms.origin.DistanceSqr(backEnd.view->origin) < radius * radius ? true : false;
            } else {
                Vec3 p = (backEnd.view->origin - light->def->parms.origin) * light->def->parms.axis;
                p /= light->def->parms.value;
                insideLightVolume = p.LengthSqr() <= 1.0f ? true : false;
            }
        } else if (light->def->parms.type == LT_PROJECTED) {
            insideLightVolume = !light->def->frustum.CullPoint(backEnd.view->origin);
        } else if (light->def->parms.type == LT_DIRECTIONAL) {
            insideLightVolume = light->def->obb.IsContainPoint(backEnd.view->origin);
        } else {
            assert(0);
        }
        
        RB_DrawStencilLightVolume(light, insideLightVolume);

        glr.SetStateBits(0);
        glr.SetCullFace(BackCull);
        glr.SetStencilState(backEnd.stencilStates[VolumeIntersectionTest], 1);
                
        glr.BeginQuery(lightQuery->queryHandle);
        RB_DrawLightVolume(light->def);
        glr.EndQuery();
    }

    glr.SetStencilState(NullStencilState, 0);

    if (r_useLightScissors.GetBool()) {
        glr.SetScissor(prevScissorRect);
    }

    if (r_showLights.GetInteger() > 0) {
        BE_LOG(L"lights: %i/%i (visible/total), queries: %i/%i (result/wait)\n", numVisLights, numLights, numQueryResult, numQueryWait);
    }*/
}

static void RB_RenderOcclusionMap(int numDrawSurfs, DrawSurf **drawSurfs) {
    Rect prevViewportRect = glr.GetViewport();

    backEnd.ctx->homRT->Begin();
    backEnd.ctx->homRT->Clear(Color4::red, 1.0f, 0);
    glr.SetViewport(Rect(0, 0, backEnd.ctx->homRT->GetWidth(), backEnd.ctx->homRT->GetHeight()));

    RB_OccluderPass(numDrawSurfs, drawSurfs);

    backEnd.ctx->homRT->End();
    glr.SetViewport(prevViewportRect);
}

static void RB_GenerateOcclusionMapHierarchy() {
    int startTime = PlatformTime::Milliseconds();

    Rect prevViewportRect = glr.GetViewport();

    int w = backEnd.ctx->homRT->GetWidth();
    int h = backEnd.ctx->homRT->GetHeight();

    float size = Max(w, h);
    int numLevels = (Math::Log(size) / Math::Log(2.0f));
    
    const Shader *shader = ShaderManager::generateHomShader;

    shader->Bind();

    Vec2 texelSize;

    for (int i = 1; i < numLevels; i++) {
        int lastMipLevel = i - 1;
        
        texelSize.Set(1.0f / w, 1.0f / h);

        w >>= 1;
        h >>= 1;
        w = w == 0 ? 1 : w;
        h = h == 0 ? 1 : h;

        backEnd.ctx->homTexture->Bind();
        glr.SetTextureLevel(lastMipLevel, lastMipLevel);

        backEnd.ctx->homRT->Begin(i);

        glr.SetViewport(Rect(0, 0, w, h));
        glr.SetStateBits(Renderer::DepthWrite | Renderer::DF_Always);
        glr.SetCullFace(Renderer::NoCull);

        shader->SetTexture("lastMip", backEnd.ctx->homTexture);
        shader->SetConstant2f("texelSize", texelSize);

        RB_DrawClipRect(0.0f, 0.0f, 1.0f, 1.0f);

        backEnd.ctx->homRT->End();
    }

    backEnd.ctx->homTexture->Bind();
    glr.SetTextureLevel(0, numLevels);

    glr.SetViewport(prevViewportRect);

    backEnd.ctx->renderCounter.homGenMsec = PlatformTime::Milliseconds() - startTime;
}

static void RB_QueryOccludeeAABBs(int numAmbientOccludees, const AABB *occludeeAABB) {
    int startTime = PlatformTime::Milliseconds();

    // alloc ambient occludee buffer in a AABB form
    struct Occludee { Vec2 position; Vec3 center; Vec3 extents; };
    Occludee *occludeeBuffer = (Occludee *)_alloca(numAmbientOccludees * sizeof(Occludee));
    Occludee *occludeePtr = occludeeBuffer;
    int x = 0;
    int y = 0;

    for (int i = 0; i < numAmbientOccludees; i++) {
        occludeePtr->position.x = (float)x / backEnd.homCullingOutputRT->GetWidth();
        occludeePtr->position.y = (float)y / backEnd.homCullingOutputRT->GetHeight();
        occludeePtr->position = occludeePtr->position * 2 - Vec2(1);
        occludeePtr->center = occludeeAABB[i].Center();
        occludeePtr->extents = occludeeAABB[i].Extents();
        occludeePtr++;

        x++;

        if (x >= backEnd.homCullingOutputRT->GetWidth()) {
            x = 0;
            y++;
        }
    }

    Rect prevViewportRect = glr.GetViewport();

    // Query HOM culling for each occludees
    backEnd.homCullingOutputRT->Begin();

    const Shader *shader = ShaderManager::queryHomShader;

    shader->Bind();
    shader->SetTexture("homap", backEnd.ctx->homTexture);
    shader->SetConstant4x4f("viewProjectionMatrix", true, backEnd.view->def->viewProjMatrix);	
    shader->SetConstant2f("viewportSize", Vec2(backEnd.ctx->homRT->GetWidth(), backEnd.ctx->homRT->GetHeight()));
    int size = Max(backEnd.ctx->homRT->GetWidth(), backEnd.ctx->homRT->GetHeight());
    int maxLevel = Math::Log(2, size) - 1;
    shader->SetConstant1i("maxLevel", maxLevel);

    glr.SetViewport(Rect(0, 0, backEnd.homCullingOutputRT->GetWidth(), backEnd.homCullingOutputRT->GetHeight()));
    glr.SetStateBits(Renderer::ColorWrite | Renderer::AlphaWrite);
    glr.SetCullFace(Renderer::NoCull);

    glr.BindBuffer(Renderer::VertexBuffer, bufferCacheManager.streamVertexBuffer);
    glr.BufferDiscardWrite(bufferCacheManager.streamVertexBuffer, numAmbientOccludees * sizeof(occludeeBuffer[0]), occludeeBuffer);

    glr.SetVertexFormat(vertexFormats[VertexFormat::Occludee].vertexFormatHandle);
    glr.SetStreamSource(0, bufferCacheManager.streamVertexBuffer, 0, sizeof(occludeeBuffer[0]));
    glr.DrawArrays(Renderer::PointsPrim, 0, numAmbientOccludees);

    backEnd.homCullingOutputRT->End();

    glr.SetViewport(prevViewportRect);

    backEnd.ctx->renderCounter.homQueryMsec = PlatformTime::Milliseconds() - startTime;
}

static void RB_MarkOccludeeVisibility(int numAmbientOccludees, const int *occludeeSurfIndexes, int numDrawSurfs, DrawSurf **drawSurfs) {	
    int startTime = PlatformTime::Milliseconds();

    // write back for visibility information to each surf
    int size = backEnd.homCullingOutputTexture->MemRequired(false);
    byte *visibilityBuffer = (byte *)_alloca(size);
    backEnd.homCullingOutputTexture->Bind();
    backEnd.homCullingOutputTexture->GetTexels2D(Image::RGBA_8_8_8_8, visibilityBuffer);
    byte *visibilityPtr = visibilityBuffer;

    for (int i = 0; i < numAmbientOccludees; i++) {
        int index = occludeeSurfIndexes[i];
        DrawSurf *surf = drawSurfs[index];

        if (visibilityPtr[2] == 0) {
            viewEntity_t *entity = surf->entity;

            surf->flags &= ~DrawSurf::AmbientVisible;

            if (entity->def->parms.joints) {
                int sameEntityIndex = index + 1;
                while (sameEntityIndex < numDrawSurfs) {
                    surf = drawSurfs[sameEntityIndex];
                    if (surf->entity != entity) {
                        break;
                    }

                    surf->flags &= ~DrawSurf::AmbientVisible;
                    sameEntityIndex++;
                }
            }
        }
        visibilityPtr += 4;
    }

    backEnd.ctx->renderCounter.homCullMsec = PlatformTime::Milliseconds() - startTime;
}

static void RB_TestOccludeeBounds(int numDrawSurfs, DrawSurf **drawSurfs) {
    viewEntity_t *prevEntity = nullptr;

    // count ambient occludees for culling
    AABB *occludeeAABB = (AABB *)_alloca(numDrawSurfs * sizeof(AABB));
    int *occludeeSurfIndexes = (int *)_alloca(numDrawSurfs * sizeof(int));
    int numAmbientOccludees = 0;

    Plane nearPlane = backEnd.view->def->frustumPlanes[0];
    nearPlane.Normalize();
    nearPlane.TranslateSelf(backEnd.view->def->parms.origin);
    
    for (int i = 0; i < numDrawSurfs; i++) {
        const DrawSurf *surf = drawSurfs[i];
        if (!(surf->flags & DrawSurf::AmbientVisible)) {
            continue;
        }

        viewEntity_t *entity = surf->entity;

        if (entity->def->parms.joints) {
            if (entity == prevEntity) {
                continue;
            }
            occludeeAABB[numAmbientOccludees].SetFromTransformedAABB(entity->def->GetAABB(), entity->def->parms.origin, entity->def->parms.axis);
        } else {	
            occludeeAABB[numAmbientOccludees].SetFromTransformedAABB(surf->subMesh->GetAABB() * entity->def->parms.scale, entity->def->parms.origin, entity->def->parms.axis);
        }
        
        //BE_LOG(L"%.2f %.2f %.2f %.2f\n", nearPlane.a, nearPlane.b, nearPlane.c, nearPlane.d);
        if (occludeeAABB[numAmbientOccludees].PlaneSide(nearPlane) != Plane::Side::Back) {
            continue;
        }
        
        occludeeSurfIndexes[numAmbientOccludees] = i;
        numAmbientOccludees++;

        prevEntity = entity;
    }

    if (numAmbientOccludees == 0) {
        return;
    }

    RB_QueryOccludeeAABBs(numAmbientOccludees, occludeeAABB);

    RB_MarkOccludeeVisibility(numAmbientOccludees, occludeeSurfIndexes, numDrawSurfs, drawSurfs);
}

static void RB_ClearView() {
    int clearBits = 0;
    
    if (backEnd.view->def->parms.clearMethod == SceneView::DepthOnlyClear) {
        clearBits = Renderer::DepthBit | Renderer::StencilBit;
    } else if (backEnd.view->def->parms.clearMethod == SceneView::ColorClear) {
        clearBits = Renderer::DepthBit | Renderer::StencilBit | Renderer::ColorBit;
        Color4 clearColor = backEnd.view->def->parms.clearColor;

        glr.SetStateBits(glr.GetStateBits() | Renderer::DepthWrite | Renderer::ColorWrite | Renderer::AlphaWrite);
        glr.Clear(clearBits, clearColor, 1.0f, 0);
    } else {
        glr.SetStateBits(glr.GetStateBits() | Renderer::DepthWrite);
        glr.Clear(clearBits, Color4::black, 1.0f, 0);
    }
}

// FIXME: subview 일 경우를 생각
static void RB_DrawView() {
    if (backEnd.ctx->flags & RenderContext::UseSelectionBuffer) {
        backEnd.ctx->screenSelectionRT->Begin();

        float scaleX = (float)backEnd.ctx->screenSelectionRT->GetWidth() / backEnd.ctx->screenRT->GetWidth();
        float scaleY = (float)backEnd.ctx->screenSelectionRT->GetHeight() / backEnd.ctx->screenRT->GetHeight();

        Rect renderRect;
        renderRect.x = backEnd.renderRect.x * scaleX;
        renderRect.y = backEnd.renderRect.y * scaleY;
        renderRect.w = backEnd.renderRect.w * scaleX;
        renderRect.h = backEnd.renderRect.h * scaleY;

        glr.SetViewport(renderRect);
        glr.SetScissor(renderRect);
        glr.SetDepthRange(0, 1);

        glr.SetStateBits(Renderer::DepthWrite | Renderer::ColorWrite | Renderer::AlphaWrite);
        glr.Clear(Renderer::ColorBit | Renderer::DepthBit, Color4::white, 1.0f, 0);

        RB_SelectionPass(backEnd.numDrawSurfs, backEnd.drawSurfs);

        backEnd.ctx->screenSelectionRT->End();
    }

    backEnd.ctx->screenRT->Begin();

    glr.SetViewport(backEnd.renderRect);
    glr.SetScissor(backEnd.renderRect);
    glr.SetDepthRange(0, 1);

    RB_ClearView();

    if (!(backEnd.view->def->parms.flags & SceneView::WireFrameMode)) {
        if (r_HOM.GetBool()) {
            // Render occluder to HiZ occlusion buffer
            RB_RenderOcclusionMap(backEnd.numDrawSurfs, backEnd.drawSurfs);

            // Generate depth hierarchy
            RB_GenerateOcclusionMapHierarchy();

            // Test all the ambient occludee's AABB using HiZ occlusion buffer
            RB_TestOccludeeBounds(backEnd.numDrawSurfs, backEnd.drawSurfs);
        }

        // Render depth only first for early-z culling
        if (r_useDepthPrePass.GetBool()) {
            RB_DepthPrePass(backEnd.numDrawSurfs, backEnd.drawSurfs);
        }

        // Render all solid (non-translucent) geometry (ambient + [velocity] + depth)
        if (!r_skipAmbientPass.GetBool()) {
            RB_AmbientPass(backEnd.numDrawSurfs, backEnd.drawSurfs);
        }

        // depth buffer 와 관련된 post processing 을 먼저 한다
        if (!(backEnd.view->def->parms.flags & SceneView::SkipPostProcess) && r_usePostProcessing.GetBool()) {
            RB_PostProcessDepth();
        }

        // Render all shadow and light interaction
        if (!r_skipShadowAndLitPass.GetBool()) {
            RB_AllShadowAndLitPass(backEnd.viewLights);
        }

        // Render wireframe for option
        RB_DrawTris(backEnd.numDrawSurfs, backEnd.drawSurfs, false);

        // Render debug tools
        if (!(backEnd.view->def->parms.flags & SceneView::SkipDebugDraw)) {
            RB_DebugPass(backEnd.numDrawSurfs, backEnd.drawSurfs);
        }

        // Render any stage with blend surface
        if (!r_skipBlendPass.GetBool()) {
            RB_BlendPass(backEnd.numDrawSurfs, backEnd.drawSurfs);
        }

        // Render to velocity map
        if (!(backEnd.view->def->parms.flags & SceneView::SkipPostProcess) && r_usePostProcessing.GetBool() && (r_motionBlur.GetInteger() & 2)) {
            RB_VelocityMapPass(backEnd.numDrawSurfs, backEnd.drawSurfs);
        }

        // Render no lighting interaction surfaces
        if (!r_skipFinalPass.GetBool()) {
            RB_FinalPass(backEnd.numDrawSurfs, backEnd.drawSurfs);
        }
    } else {
        RB_DrawTris(backEnd.numDrawSurfs, backEnd.drawSurfs, true);

        // Render debug tools
        if (!(backEnd.view->def->parms.flags & SceneView::SkipDebugDraw)) {
            RB_DebugPass(backEnd.numDrawSurfs, backEnd.drawSurfs);
        }
    }

    backEnd.ctx->screenRT->End();

    // Post process & upscale
    Rect upscaleRect = backEnd.renderRect;
    upscaleRect.x = Math::Rint(upscaleRect.x * backEnd.upscaleFactor.x);
    upscaleRect.y = Math::Rint(upscaleRect.y * backEnd.upscaleFactor.y);
    upscaleRect.w = Math::Rint(upscaleRect.w * backEnd.upscaleFactor.x);
    upscaleRect.h = Math::Rint(upscaleRect.h * backEnd.upscaleFactor.y);

    glr.SetViewport(upscaleRect);
    glr.SetScissor(upscaleRect);

    if (!(backEnd.view->def->parms.flags & SceneView::SkipPostProcess) && r_usePostProcessing.GetBool()) {
        RB_PostProcess();
    } else {
        glr.SetStateBits(Renderer::ColorWrite | Renderer::AlphaWrite);
        glr.SetCullFace(Renderer::NoCull);

        const Shader *shader = ShaderManager::postPassThruShader;

        shader->Bind();
        shader->SetTexture("tex0", backEnd.ctx->screenRT->ColorTexture());

        float screenTc[4];
        screenTc[0] = (float)backEnd.renderRect.x / backEnd.ctx->screenRT->GetWidth();
        screenTc[1] = (float)backEnd.renderRect.y / backEnd.ctx->screenRT->GetHeight();
        screenTc[2] = screenTc[0] + (float)backEnd.renderRect.w / backEnd.ctx->screenRT->GetWidth();
        screenTc[3] = screenTc[1] + (float)backEnd.renderRect.h / backEnd.ctx->screenRT->GetHeight();

        RB_DrawClipRect(screenTc[0], screenTc[1], screenTc[2], screenTc[3]);
    }

    backEnd.viewMatrixPrev = backEnd.view->def->viewMatrix;

    glr.SetScissor(Rect::empty);
}

static void RB_Draw2DView() {
    if (!backEnd.numDrawSurfs) {
        return;
    }
    
    glr.SetViewport(backEnd.screenRect);
    glr.SetScissor(backEnd.screenRect);
    glr.SetDepthRange(0, 0);
    
    RB_GuiPass(backEnd.numDrawSurfs, backEnd.drawSurfs);

    glr.SetScissor(Rect::empty);
}

static const void *RB_ExecuteDrawView(const void *data) {
    DrawViewRenderCommand *cmd = (DrawViewRenderCommand *)data;

    backEnd.view            = &cmd->view;
    backEnd.time            = MS2SEC(cmd->view.def->parms.time);
    backEnd.viewEntities    = cmd->view.viewEntities;
    backEnd.viewLights      = cmd->view.viewLights;
    backEnd.numDrawSurfs    = cmd->view.numDrawSurfs;
    backEnd.drawSurfs       = cmd->view.drawSurfs;
    backEnd.projMatrix      = cmd->view.def->projMatrix;
    backEnd.renderRect      = cmd->view.def->parms.renderRect;
    backEnd.upscaleFactor   = Vec2(backEnd.ctx->GetUpscaleFactorX(), backEnd.ctx->GetUpscaleFactorY());
    backEnd.mainLight       = nullptr;

    backEnd.screenRect.Set(0, 0, backEnd.ctx->GetDeviceWidth(), backEnd.ctx->GetDeviceHeight());

    // NOTE: glViewport() 의 y 는 밑에서부터 증가되므로 여기서 뒤집어 준다
    backEnd.renderRect.y	= backEnd.ctx->GetRenderHeight() - backEnd.renderRect.Y2();
    backEnd.screenRect.y	= backEnd.ctx->GetDeviceHeight() - backEnd.screenRect.Y2();
    
    if (backEnd.view->is2D) {
        RB_Draw2DView();
    } else {
        RB_DrawView();
    }

    return (const void *)(cmd + 1);
}

static const void *RB_ExecuteScreenshot(const void *data) {
    ScreenShotRenderCommand *cmd = (ScreenShotRenderCommand *)data;

    if (cmd->x + cmd->width > backEnd.ctx->GetDeviceWidth() || cmd->y + cmd->height > backEnd.ctx->GetDeviceHeight()) {
        BE_WARNLOG(L"larger than screen size: %i, %i, %i, %i\n", cmd->x, cmd->y, cmd->width, cmd->height);
    }
    
    Image screenImage;
    screenImage.Create2D(cmd->width, cmd->height, 1, Image::BGR_8_8_8, nullptr, Image::SRGBFlag);
    glr.ReadPixels(cmd->x, cmd->y, cmd->width, cmd->height, Image::BGR_8_8_8, screenImage.GetPixels());
    screenImage.FlipY();

    // apply gamma ramp table
    if (r_gamma.GetBool() != 1.0f) {
        unsigned short ramp[768];
        glr.GetGammaRamp(ramp);
        screenImage.GammaCorrectRGB888(ramp);
    }

    Str filename = cmd->filename;
    filename.DefaultFileExtension(".png");

    screenImage.Write(filename);
/*
    int sw = (cmd->width + 15) & ~15;
    int sh = (cmd->height + 15) & ~15;

    byte *temp = (byte *)Mem_Alloc16(sw * sh * 3);
    byte *temp2 = (byte *)Mem_Alloc16(sw * sh * 3);

    simdProcessor->Memset(temp2, 0, sw * sh * 3);

    Image_Scale(src, cmd->width, cmd->height, temp, sw, sh, Image::BGRA_8_8_8_8, Image::BicubicFilter);
    Image_Write("ycocgtest0.tga", temp, sw, sh, Image::BGRA_8_8_8_8);

    int YCoCgSize = 3 * sw * sh;
    short *YCoCg = (short *)Mem_Alloc16(YCoCgSize);

    int wblocks = sw >> 4;
    int hblocks = sh >> 4;
    
    for (int j = 0; j < hblocks; j++) {
        for (int i = 0; i < wblocks; i++) {
            simdProcessor->RGBToYCoCg(temp + 16 * 3 * (j * sw + i), sw * 3, YCoCg + 384 * (j * wblocks + i));
            simdProcessor->YCoCgToRGB(YCoCg + 384 * (j * wblocks + i), temp2 + 16 * 3 * (j * sw + i), sw * 3);
        }
    }
    
    Image_Write("ycocgtest1.tga", temp2, sw, sh, Image::BGRA_8_8_8_8);

    Mem_Free16(YCoCg);
    Mem_Free16(temp);
    Mem_Free16(temp2);*/

    return (const void *)(cmd + 1);
}

void RB_DrawDebugTextures() {
    int start = 0;
    int end = textureManager.textureHashMap.Count();
    
    float w = 40.0f;
    float h = 40.0f;

    backEnd.ctx->AdjustFrom640x480(nullptr, nullptr, &w, &h);

    int x = 0;
    int y = 0;

    glr.SetStateBits(Renderer::ColorWrite | Renderer::BS_SrcAlpha | Renderer::BD_OneMinusSrcAlpha);	
    
    for (int i = start; i < end; i++) {
        const auto *entry = textureManager.textureHashMap.GetByIndex(i);
        Texture *texture = entry->second;

        if (!texture) {
            continue;
        }

        if (texture->GetType() == Renderer::TextureBuffer || texture->GetType() == Renderer::Texture2DArray) {
            // do nothing
        } else {
            const Shader *shader = ShaderManager::postPassThruShader;

            shader->Bind();
            shader->SetTexture("tex0", texture);

            if (texture->GetFlags() & Texture::Shadow) {
                texture->Bind();
                glr.SetTextureShadowFunc(false);
            }

            if (texture->GetType() == Renderer::TextureRectangle) {
                RB_DrawScreenRect(x, y, w, h, 0.0f, 0.0f, texture->GetWidth(), texture->GetHeight());
            } else {
                RB_DrawScreenRect(x, y, w, h, 0.0f, 0.0f, 1.0f, 1.0f);
            }

            if (texture->GetFlags() & Texture::Shadow) {
                glr.SetTextureShadowFunc(true);
            }
        }

        x += w;

        if ((int)(x + w) > backEnd.ctx->GetDeviceWidth()) {
            x = 0.0f;
            y += h;
        }

        if (y > backEnd.ctx->GetDeviceHeight()) {
            break;
        }
    }
}

static void RB_DrawDebugShadowMap() {
    glr.SetStateBits(Renderer::ColorWrite);

    if (r_showShadows.GetInteger() == 1) {
        float w = 100.0f;
        float h = 100.0f;

        backEnd.ctx->AdjustFrom640x480(nullptr, nullptr, &w, &h);

        float space = 1;
        float x = space;
        float y = space;

        const Texture *shadowTexture = backEnd.ctx->shadowMapRT->DepthStencilTexture();

        shadowTexture->Bind();
        glr.SetTextureShadowFunc(false);

        const Shader *shader = ShaderManager::drawArrayTextureShader;

        shader->Bind();
        shader->SetTexture("tex0", shadowTexture);

        for (int i = 0; i < r_CSM_count.GetInteger(); i++) {
            RB_DrawScreenRectSlice(x, y, w, h, 0.0f, 0.0f, 1.0f, 1.0f, i);
            x += w + space;
        }

        glr.SetTextureShadowFunc(true);

        x = space;
        y += h + space;
    } else if (r_showShadows.GetInteger() == 2) {
        float w = 200.0f;
        float h = 200.0f;

        backEnd.ctx->AdjustFrom640x480(nullptr, nullptr, &w, &h);

        float space = 1;
        float x = space;
        float y = space;

        const Texture *shadowTexture = backEnd.ctx->vscmRT->DepthStencilTexture();

        shadowTexture->Bind();
        glr.SetTextureShadowFunc(false);

        const Shader *shader = ShaderManager::postPassThruShader;
        
        shader->Bind();
        shader->SetTexture("tex0", shadowTexture);

        RB_DrawScreenRect(x, y, w, h, 0.0f, 0.0f, 1.0f, 1.0f);

        glr.SetTextureShadowFunc(true);

        x += w + space;

        if (x + w > backEnd.ctx->GetDeviceWidth()) {
            x = space;
            y += h + space;
        }
    }
}

void RB_DrawRenderTargetTexture() {
    float x, y, w, h;

    if (r_showRenderTarget_fullscreen.GetBool()) {
        x = 0.0f;
        y = 0.0f;

        w = backEnd.ctx->GetDeviceWidth();
        h = backEnd.ctx->GetDeviceHeight();
    } else {
        x = 10.0f;
        y = 10.0f;

        w = backEnd.ctx->GetDeviceWidth() / 4;
        h = backEnd.ctx->GetDeviceHeight() / 4;
    }

    glr.SetStateBits(Renderer::ColorWrite);
    
    int index = r_showRenderTarget.GetInteger() - 1;
    if (index < RenderTarget::rts.Count()) {
        const RenderTarget *rt = RenderTarget::rts[index];
        if (rt && rt->ColorTexture()) {
            const Shader *shader = ShaderManager::postPassThruShader;
            
            shader->Bind();
            shader->SetTexture("tex0", rt->ColorTexture());
            
            RB_DrawScreenRect(x, y, w, h, 0.0f, 1.0f, 1.0f, 0.0f);
        }
    }
}

void RB_DrawDebugHdrMap() {
    int space = 10;

    float x = space;
    float y = 100.0f + space;

    float w = 100.0f;
    float h = 100.0f;

    glr.SetStateBits(Renderer::ColorWrite);

    const Shader *shader = ShaderManager::postPassThruShader;

    shader->Bind();
    shader->SetTexture("tex0", backEnd.ctx->screenColorTexture);
            
    RB_DrawScreenRect(x, y, w, h, 0.0f, 1.0f, 1.0f, 0.0f);
    
    x += w + space;

    for (int i = 0; i < COUNT_OF(backEnd.ctx->hdrBloomTexture); i++) {
        shader->Bind();
        shader->SetTexture("tex0", backEnd.ctx->hdrBloomTexture[i]);
            
        RB_DrawScreenRect(x, y, w, h, 0.0f, 1.0f, 1.0f, 0.0f);

        x += w + space;
    }

    y += h + space;
    x = space;

    for (int i = 0; i < COUNT_OF(backEnd.ctx->hdrLumAverageTexture); i++) {
        if (!backEnd.ctx->hdrLumAverageTexture[i]) {
            break;
        }

        shader->Bind();
        shader->SetTexture("tex0", backEnd.ctx->hdrLumAverageTexture[i]);
            
        RB_DrawScreenRect(x, y, w, h, 0.0f, 1.0f, 1.0f, 0.0f);

        x += w + space;
    }

    for (int i = 0; i < COUNT_OF(backEnd.ctx->hdrLuminanceTexture); i++) {
        shader->Bind();
        shader->SetTexture("tex0", backEnd.ctx->hdrLuminanceTexture[i]);
            
        RB_DrawScreenRect(x, y, w, h, 0.0f, 1.0f, 1.0f, 0.0f);

        x += w + space;
    }
}

static void RB_DrawDebugHOMap() {
    int space = 10;

    float x = space;
    float y = space;

    float w = 100.0f;
    float h = 100.0f;

    float size = Max(backEnd.ctx->homRT->GetWidth(), backEnd.ctx->homRT->GetHeight());
    int numLevels = Math::Log(2, size);

    glr.SetStateBits(Renderer::ColorWrite);

    for (int i = 0; i < numLevels; i++) {
        backEnd.ctx->homRT->DepthStencilTexture()->Bind();
        glr.SetTextureLevel(i, i);

        const Shader *shader = ShaderManager::postPassThruShader;
        
        shader->Bind();
        shader->SetTexture("tex0", backEnd.ctx->homRT->DepthStencilTexture());
        //shader->SetConstant2f("depthRange", Vec2(backEnd.view->zNear, backEnd.view->zFar));

        RB_DrawScreenRect(x, y, w, h, 0.0f, 1.0f, 1.0f, 0.0f);

        x += w + space;
    }

    backEnd.ctx->homRT->DepthStencilTexture()->Bind();
    glr.SetTextureLevel(0, numLevels);
}

static const void *RB_ExecuteSwapBuffers(const void *data) {
    SwapBuffersRenderCommand *cmd = (SwapBuffersRenderCommand *)data;

    // 남아있는 폴리곤들을 마저 그린다
    backEnd.rbsurf.Flush();

    backEnd.rbsurf.EndFrame();    

    glr.SetViewport(backEnd.screenRect);
    glr.SetScissor(backEnd.screenRect);
    glr.SetDepthRange(0, 0);
    glr.SetCullFace(Renderer::NoCull);

    if (r_showTextures.GetInteger() > 0) {
        RB_DrawDebugTextures();
    }

    if (r_showShadows.GetInteger() > 0 && r_shadows.GetInteger() == 1) {
        RB_DrawDebugShadowMap();
    }

    if (r_showRenderTarget.GetInteger() > 0) {
        RB_DrawRenderTargetTexture();
    }

    if (r_HDR.GetInteger() > 0 && r_HDR_debug.GetInteger() > 0) {
        RB_DrawDebugHdrMap();
    }

    if (r_HOM.GetBool() && r_HOM_debug.GetBool()) {
        RB_DrawDebugHOMap();
    }

    glr.SwapBuffers();
    
    return (const void *)(cmd + 1);
}

void RB_Execute(const void *data) {
    int t1, t2;

    t1 = PlatformTime::Milliseconds();

    backEnd.rbsurf.Begin(RBSurf::FinalFlush, nullptr, nullptr, nullptr, nullptr);
    
    while (1) {
        int cmd = *(const int *)data;
        switch (cmd) {
        case BeginContextCommand:
            data = RB_ExecuteBeginContext(data);
            continue;
        case DrawViewCommand:
            data = RB_ExecuteDrawView(data);
            continue;
        case ScreenShotCommand:
            data = RB_ExecuteScreenshot(data);
            continue;
        case SwapBuffersCommand:
            data = RB_ExecuteSwapBuffers(data);
            continue;
        case EndOfCommand:
            t2 = PlatformTime::Milliseconds();
            backEnd.ctx->renderCounter.backEndMsec = t2 - t1;
            return;
        }
    }
}

BE_NAMESPACE_END
