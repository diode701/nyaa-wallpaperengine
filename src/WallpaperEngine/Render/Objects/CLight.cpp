#include "CLight.h"

#include <glm/gtc/matrix_transform.hpp>

using namespace WallpaperEngine::Render::Objects;

namespace {
float readFloat (const UserSettingUniquePtr& setting) {
    if (!setting || !setting->value) {
	return 0.0f;
    }

    return setting->value->getFloat ();
}

glm::vec3 readVec3 (const UserSettingUniquePtr& setting, const glm::vec3& fallback) {
    if (!setting || !setting->value) {
	return fallback;
    }

    return setting->value->getVec3 ();
}
}

CLight::CLight (Wallpapers::CScene& scene, const Light& light) : CObject (scene, light), m_light (light) { }

const Light& CLight::getLight () const { return this->m_light; }

glm::vec3 CLight::getOrigin () const { return readVec3 (this->m_light.origin, glm::vec3 (0.0f)); }

glm::vec3 CLight::getColor () const { return readVec3 (this->m_light.color, glm::vec3 (1.0f)); }

float CLight::getIntensity () const { return readFloat (this->m_light.intensity); }

float CLight::getRadius () const { return readFloat (this->m_light.radius); }

float CLight::getExponent () const { return this->m_light.exponent; }

float CLight::getInnerCone () const { return this->m_light.innerCone; }

float CLight::getOuterCone () const { return this->m_light.outerCone; }

glm::vec3 CLight::getDirection () const {
    const glm::vec3 angles = readVec3 (this->m_light.groupAngles, glm::vec3 (0.0f));

    glm::mat4 rotation (1.0f);
    rotation = glm::rotate (rotation, glm::radians (angles.z), glm::vec3 (0.0f, 0.0f, 1.0f));
    rotation = glm::rotate (rotation, glm::radians (angles.y), glm::vec3 (0.0f, 1.0f, 0.0f));
    rotation = glm::rotate (rotation, glm::radians (angles.x), glm::vec3 (1.0f, 0.0f, 0.0f));

    return glm::normalize (glm::vec3 (rotation * glm::vec4 (1.0f, 0.0f, 0.0f, 0.0f)));
}
