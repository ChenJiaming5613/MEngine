// Frame graph viewer descriptions and parameter panels of the renderer's passes, kept apart from the passes' GPU code so
// only this file depends on Dear ImGui. Every panel edits the RenderSettings fields its pass consumes, the same data the
// viewer's Lighting / Debug panels edit, so both stay in sync.

#include "render/RenderTypes.hpp"
#include "render/passes/DebugDrawPass.hpp"
#include "render/passes/ForwardPbrPass.hpp"
#include "render/passes/ShadowPass.hpp"
#include "render/passes/SkyboxPass.hpp"
#include "render/passes/TonemapPass.hpp"

#include <glm/geometric.hpp>

#if defined(_MSC_VER)
#pragma warning(push, 0)
#elif defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif
#include <imgui.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__clang__)
#pragma clang diagnostic pop
#endif

namespace mengine::render {

namespace {

// Kept normalized; a zero vector keeps the previous direction.
void EditDirection(const char* label, glm::vec3& direction)
{
    glm::vec3 edited = direction;
    if (ImGui::DragFloat3(label, &edited.x, 0.005f, -1.0f, 1.0f, "%.3f"))
    {
        const float length = glm::length(edited);
        if (length > 1e-4f)
        {
            direction = edited / length;
        }
    }
}

} // namespace

// ---------------------------------------------------------------------------------------------------------------------
// TonemapPass

const char* TonemapPass::Description() const
{
    return "Applies exposure and tone-maps the HDR color into the backbuffer.";
}

bool TonemapPass::DrawInspector(RenderSettings& settings)
{
    ImGui::SliderFloat("Exposure", &settings.exposure, 0.1f, 32.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// SkyboxPass

const char* SkyboxPass::Description() const
{
    return "Draws the environment behind the scene as a far plane full screen triangle.";
}

bool SkyboxPass::DrawInspector(RenderSettings& settings)
{
    ImGui::SliderFloat("Skybox Blur", &settings.skyboxBlur, 0.0f, 1.0f, "%.3f");
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// ForwardPbrPass

const char* ForwardPbrPass::Description() const
{
    return "Shades the draws inside the camera frustum with the point light, the shadowed directional light and IBL.";
}

bool ForwardPbrPass::DrawInspector(RenderSettings& settings)
{
    ImGui::Checkbox("Frustum Culling", &settings.frustumCulling);
    ImGui::SameLine();
    ImGui::Checkbox("Freeze", &settings.freezeCulling);
    ImGui::Text("Draws: %u / %u", m_drawCount, m_sceneDrawCount);
    ImGui::SliderFloat("IBL Intensity", &settings.iblIntensity, 0.0f, 4.0f, "%.2f");

    DirectionalLight& directional = settings.directional;
    ImGui::PushID("Directional");
    ImGui::SeparatorText("Directional Light");
    ImGui::ColorEdit3("Color", &directional.color.x);
    ImGui::DragFloat("Intensity", &directional.intensity, 0.02f, 0.0f, 1000.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
    EditDirection("Direction", directional.direction);
    ImGui::PopID();

    PointLight& light = settings.light;
    ImGui::PushID("Point");
    ImGui::SeparatorText("Point Light");
    ImGui::DragFloat3("Position", &light.position.x, 0.05f);
    ImGui::ColorEdit3("Color", &light.color.x);
    ImGui::DragFloat("Intensity", &light.intensity, 0.5f, 0.0f, 100000.0f, "%.1f", ImGuiSliderFlags_Logarithmic);
    ImGui::DragFloat("Radius", &light.radius, 0.1f, 0.01f, 10000.0f, "%.2f");
    ImGui::PopID();
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// ShadowPass

const char* ShadowPass::Description() const
{
    return "Renders the directional light's depth-only shadow map; re-rendered only when the light or scene changes.";
}

bool ShadowPass::DrawInspector(RenderSettings& settings)
{
    DirectionalLight& directional = settings.directional;
    ImGui::Checkbox("Cast Shadows", &directional.castShadows);
    ImGui::SliderFloat("Normal Offset", &directional.normalOffsetTexels, 0.0f, 5.0f, "%.2f texels");
    EditDirection("Light Direction", directional.direction);

    ImGui::SeparatorText("Shadow Map");
    ImGui::Text("Size: %u x %u (D16)", kSize, kSize);
    // Prepare() runs inside Render, after the UI, so this is the content the last executed frame left in the map.
    ImGui::Text("Content: %s", !m_rendered ? "not rendered yet" : m_castersDrawn ? "scene casters" : "cleared (shadows off)");
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// DebugDrawPass

const char* DebugDrawPass::Description() const
{
    return "Draws the debug lines into the HDR target after the skybox; inactive in frames without lines.";
}

bool DebugDrawPass::DrawInspector(RenderSettings& settings)
{
    ImGui::Checkbox("Scene Bounds", &settings.debugSceneBounds);
    ImGui::Checkbox("Shadow Frustum", &settings.debugShadowFrustum);
    ImGui::Checkbox("Draw Bounds", &settings.debugDrawBounds);
    return true;
}

} // namespace mengine::render
