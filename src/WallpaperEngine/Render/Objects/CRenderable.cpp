#include "CRenderable.h"

#include "WallpaperEngine/Data/Model/Material.h"
#include "WallpaperEngine/Data/Model/Object.h"
#include "WallpaperEngine/Data/Model/Project.h"
#include "WallpaperEngine/Data/Model/Property.h"
#include "WallpaperEngine/Data/Model/Wallpaper.h"
#include "WallpaperEngine/Data/Parsers/MaterialParser.h"

using namespace WallpaperEngine;
using namespace WallpaperEngine::Render::Objects;
using namespace WallpaperEngine::Render::Objects::Effects;
using namespace WallpaperEngine::Data::Parsers;
using namespace WallpaperEngine::Data::Builders;

CRenderable::CRenderable (Wallpapers::CScene& scene, const Object& object, const Material& material) :
    CObject (scene, object), Render::FBOProvider (&scene), m_material (material) { }

std::optional<std::string> CRenderable::resolveUserTexture (const std::string& propertyName) const {
    if (propertyName.empty ()) {
	return std::nullopt;
    }

    const auto& properties = this->getScene ().getScene ().project.properties;
    const auto it = properties.find (propertyName);

    if (it == properties.end () || it->second == nullptr) {
	return std::nullopt;
    }

    const auto* sceneTexture = dynamic_cast<const PropertySceneTexture*> (it->second.get ());

    if (sceneTexture == nullptr) {
	return std::nullopt;
    }

    std::string value = sceneTexture->toString ();

    if (value.empty ()) {
	return std::nullopt;
    }

    return value;
}

void CRenderable::detectTexture () {
    const auto& firstPass = *this->m_material.passes.begin ();

    if (const TextureMap* textures = &firstPass->textures; !textures->empty ()) {
	std::string textureName = textures->begin ()->second;

	// the user may have replaced the main texture through a scenetexture property
	if (const auto user = firstPass->usertextures.find (textures->begin ()->first);
	    user != firstPass->usertextures.end ()) {
	    if (const auto chosen = this->resolveUserTexture (user->second); chosen.has_value ()) {
		textureName = *chosen;
		this->m_textureIsUserChoice = true;
	    }
	}

	if (textureName.find ("_rt_") == 0 || textureName.find ("_alias_") == 0) {
	    this->m_texture = this->getScene ().findFBO (textureName);
	} else {
	    this->m_texture = this->getContext ().resolveTexture (textureName);
	}
    }
}

void CRenderable::setup () {
    // calculate full animation time (if any)
    this->m_animationTime = 0.0f;

    for (const auto& cur : this->getTexture ()->getFrames ()) {
	this->m_animationTime += cur->frametime;
    }
}

std::shared_ptr<const TextureProvider> CRenderable::getTexture () const { return this->m_texture; }

double CRenderable::getAnimationTime () const { return this->m_animationTime; }