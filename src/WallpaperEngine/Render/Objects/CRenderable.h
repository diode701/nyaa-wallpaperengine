#pragma once

#include <optional>
#include <string>
#include "WallpaperEngine/Render/CObject.h"
#include "WallpaperEngine/Render/FBOProvider.h"
#include "WallpaperEngine/Render/Objects/Effects/CPass.h"
#include "WallpaperEngine/Render/Wallpapers/CScene.h"

#include "WallpaperEngine/Render/Shaders/Shader.h"

using namespace WallpaperEngine;
using namespace WallpaperEngine::Render;

namespace WallpaperEngine::Render::Objects {
class CRenderable : public CObject, public FBOProvider {
    friend CObject;

public:
    CRenderable (Wallpapers::CScene& scene, const Object& object, const Material& material);

    [[nodiscard]] std::shared_ptr<const TextureProvider> getTexture () const;

    [[nodiscard]] double getAnimationTime () const;

    virtual void setup ();

    [[nodiscard]] virtual const float& getBrightness () const = 0;
    [[nodiscard]] virtual const float& getUserAlpha () const = 0;
    [[nodiscard]] virtual const float& getAlpha () const = 0;
    [[nodiscard]] virtual const glm::vec3& getColor () const = 0;
    [[nodiscard]] virtual const glm::vec4& getColor4 () const = 0;
    [[nodiscard]] virtual const glm::vec3& getCompositeColor () const = 0;

    /**
     * A material's "usertextures" entry names a scenetexture property. When
     * the user picked an image it replaces the slot's texture; when not, the
     * slot keeps the material's default texture.
     *
     * @return the image to load, or nothing when the slot keeps its default
     */
    [[nodiscard]] std::optional<std::string> resolveUserTexture (const std::string& propertyName) const;

protected:
    void detectTexture ();

    double m_animationTime = 0.0;
    /** the main texture is an image the user picked (scenetexture), not the material's */
    bool m_textureIsUserChoice = false;

    std::shared_ptr<const TextureProvider> m_texture = nullptr;
    const Material& m_material;
};
}
