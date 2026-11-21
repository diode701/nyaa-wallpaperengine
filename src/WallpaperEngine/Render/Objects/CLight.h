#pragma once

#include "WallpaperEngine/Render/CObject.h"

using namespace WallpaperEngine;

namespace WallpaperEngine::Render::Wallpapers {
class CScene;
}

namespace WallpaperEngine::Render::Objects {
using namespace WallpaperEngine::Data::Model;

/**
 * A scene light. Lights do not draw anything: the scene packs the visible ones
 * into the shader's four light slots every frame (see CScene::updateLights).
 */
class CLight final : public CObject {
public:
    CLight (Wallpapers::CScene& scene, const Light& light);

    [[nodiscard]] const Light& getLight () const;

    [[nodiscard]] glm::vec3 getOrigin () const;
    [[nodiscard]] glm::vec3 getColor () const;
    [[nodiscard]] float getIntensity () const;
    [[nodiscard]] float getRadius () const;
    [[nodiscard]] float getExponent () const;
    [[nodiscard]] float getInnerCone () const;
    [[nodiscard]] float getOuterCone () const;
    /** The object's x axis after its angles, the direction a spot or directional light points */
    [[nodiscard]] glm::vec3 getDirection () const;

private:
    const Light& m_light;
};
} // namespace WallpaperEngine::Render::Objects
