#pragma once

#include "WallpaperEngine/Render/Camera.h"

#include "WallpaperEngine/Data/Model/ScriptedDynamicValue.h"
#include "WallpaperEngine/Render/CWallpaper.h"

namespace WallpaperEngine::Render {
class Camera;
class CObject;
}

namespace WallpaperEngine::Render::Objects {
class CLight;
}

namespace WallpaperEngine::Render::Wallpapers {
using namespace WallpaperEngine::Data::Model;

class CScene final : public CWallpaper {
public:
    CScene (
	const Wallpaper& wallpaper, RenderContext& context, AudioContext& audioContext,
	const WallpaperState::TextureUVsScaling& scalingMode, const uint32_t& clampMode
    );

    ~CScene () override;

    [[nodiscard]] Camera& getCamera () const;

    [[nodiscard]] const Scene& getScene () const;

    [[nodiscard]] int getWidth () const override;
    [[nodiscard]] int getHeight () const override;

    const glm::vec2* getMousePosition () const;
    const glm::vec2* getMousePositionLast () const;
    const glm::vec2* getMousePositionNormalized () const;
    const glm::vec2* getParallaxDisplacement () const;

    [[nodiscard]] const std::vector<CObject*>& getObjectsByRenderOrder () const;

    /** Slots per light kind; Wallpaper Engine's generated lighting takes four */
    static constexpr std::size_t MAX_LIGHTS_PER_KIND = 4;

    [[nodiscard]] int getPointLightCount () const;
    [[nodiscard]] int getSpotLightCount () const;
    [[nodiscard]] int getDirectionalLightCount () const;

    [[nodiscard]] const glm::vec4* getPointLightColors () const;
    [[nodiscard]] const glm::vec4* getPointLightOrigins () const;
    [[nodiscard]] const glm::vec4* getSpotLightColors () const;
    [[nodiscard]] const glm::vec4* getSpotLightOrigins () const;
    [[nodiscard]] const glm::vec4* getSpotLightDirections () const;
    [[nodiscard]] const glm::vec4* getSpotLightExponents () const;
    [[nodiscard]] const glm::vec4* getDirectionalLightColors () const;
    [[nodiscard]] const glm::vec4* getDirectionalLightDirections () const;
    [[nodiscard]] const glm::vec3* getEyePosition () const;

    [[nodiscard]] const CObject* getObject (int id) const;

protected:
    void renderFrame (const glm::ivec4& viewport) override;
    void updateMouse (const glm::ivec4& viewport);

    friend class CWallpaper;

private:
    Render::CObject* createObject (const Object& object);
    void addObjectToRenderOrder (const Object& object);
    void collectScriptedValues ();
    void registerScriptedValue (const UserSettingUniquePtr& setting);
    void updateScriptedValues ();
    void updateLights ();
    /**
     * Visibility of the object itself, folded with the visibility of every
     * ancestor. Scene objects are a flat list with `parent` links, so a hidden
     * group only hides its children if the chain is walked explicitly.
     */
    [[nodiscard]] bool isVisibleInHierarchy (const Object& object) const;
    /**
     * True when another scene object lists this id as a dependency. Those may
     * be invisible and still be needed as an effect source, so they keep
     * rendering even when they are not drawn themselves.
     */
    [[nodiscard]] bool isDependencySource (int objectId) const;
    /**
     * Topmost visible object under a point expressed in cursor world space
     * (0..scene width, 0..scene height).
     */
    [[nodiscard]] CObject* objectAt (const glm::vec2& worldPosition) const;
    /** Sends cursor enter/leave/move/down/up to the scripts that own objects. */
    void dispatchMouseEvents ();

    std::unique_ptr<Camera> m_camera;
    ObjectUniquePtr m_bloomObjectData;
    CObject* m_bloomObject = nullptr;
    std::map<int, CObject*> m_objects = {};
    std::vector<CObject*> m_objectsByRenderOrder = {};
    std::vector<ScriptedDynamicValue*> m_scriptedValues = {};
    std::vector<Objects::CLight*> m_lights = {};
    glm::vec4 m_pointLightColors[MAX_LIGHTS_PER_KIND] = {};
    glm::vec4 m_pointLightOrigins[MAX_LIGHTS_PER_KIND] = {};
    glm::vec4 m_spotLightColors[MAX_LIGHTS_PER_KIND] = {};
    glm::vec4 m_spotLightOrigins[MAX_LIGHTS_PER_KIND] = {};
    glm::vec4 m_spotLightDirections[MAX_LIGHTS_PER_KIND] = {};
    glm::vec4 m_spotLightExponents[MAX_LIGHTS_PER_KIND] = {};
    glm::vec4 m_directionalLightColors[MAX_LIGHTS_PER_KIND] = {};
    glm::vec4 m_directionalLightDirections[MAX_LIGHTS_PER_KIND] = {};
    int m_pointLightCount = 0;
    int m_spotLightCount = 0;
    int m_directionalLightCount = 0;
    glm::vec3 m_eyePosition = {};
    glm::vec2 m_mousePosition = {};
    glm::vec2 m_mousePositionLast = {};
    glm::vec2 m_mousePositionNormalized = {};
    glm::vec2 m_parallaxDisplacement = {};
    std::vector<CObject*> m_hoveredChain = {};
    glm::vec2 m_mouseWorldLast = {};
    bool m_mouseDownLast = false;
    std::shared_ptr<const CFBO> _rt_4FrameBuffer = nullptr;
    std::shared_ptr<const CFBO> _rt_8FrameBuffer = nullptr;
    std::shared_ptr<const CFBO> _rt_Bloom = nullptr;
    std::shared_ptr<const CFBO> _rt_shadowAtlas = nullptr;
};
} // namespace WallpaperEngine::Render::Wallpaper
