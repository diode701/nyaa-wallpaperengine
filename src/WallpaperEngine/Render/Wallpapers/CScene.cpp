#include "WallpaperEngine/Render/Objects/CImage.h"
#include "WallpaperEngine/Render/Objects/CParticle.h"
#include "WallpaperEngine/Render/Objects/CLight.h"
#include "WallpaperEngine/Render/Objects/CSound.h"
#include "WallpaperEngine/Render/Objects/CText.h"

#include "WallpaperEngine/Render/WallpaperState.h"

#include "CScene.h"
#include "WallpaperEngine/Logging/Log.h"

#include "WallpaperEngine/Data/Model/Wallpaper.h"
#include "WallpaperEngine/Data/Parsers/ObjectParser.h"
#include "WallpaperEngine/Input/InputContext.h"
#include "WallpaperEngine/Render/Shaders/ShaderUnit.h"
#include "WallpaperEngine/Scripting/ScriptEngine.h"

#include <chrono>
#include <cstdlib>
#include <ranges>

extern float g_Time;
extern float g_TimeLast;

using namespace WallpaperEngine;
using namespace WallpaperEngine::Render;
using namespace WallpaperEngine::Data::Model;
using namespace WallpaperEngine::Data::Parsers;
using namespace WallpaperEngine::Render::Wallpapers;
using JSON = WallpaperEngine::Data::JSON::JSON;

namespace {
Render::CObject* createImageObject (CScene& scene, const Image& imageData) {
    auto* image = new Objects::CImage (scene, imageData);
    try {
	image->setup ();
    } catch (std::runtime_error&) {
	sLog.error ("Cannot setup image ", image->getImage ().name);
    }
    return image;
}

Render::CObject* createParticleObject (CScene& scene, const Particle& particleData) {
    if (scene.getContext ().getApp ().getContext ().settings.general.disableParticles == true) {
	sLog.debug ("Ignoring particle system (disabled in settings): ", particleData.name);
	return nullptr;
    }

    auto* particle = new Objects::CParticle (scene, particleData);
    try {
	particle->setup ();
    } catch (std::runtime_error&) {
	sLog.error ("Cannot setup particle ", particle->getParticle ().name);
    }
    return particle;
}

bool ownVisibility (const Object& object) {
    if (object.is<Image> ()) {
	return object.as<Image> ()->visible->value->getBool ();
    }
    if (object.is<Text> ()) {
	return object.as<Text> ()->visible->value->getBool ();
    }
    if (object.is<Particle> ()) {
	return object.as<Particle> ()->visible->value->getBool ();
    }
    // group/container objects keep their transforms and visibility in the base fields
    return object.groupVisible->value->getBool ();
}
}

CScene::CScene (
    const Wallpaper& wallpaper, RenderContext& context, AudioContext& audioContext,
    const WallpaperState::TextureUVsScaling& scalingMode, const uint32_t& clampMode
) : CWallpaper (wallpaper, context, audioContext, scalingMode, clampMode) {
    // caller should check this, if not a std::bad_cast is good to throw
    auto scene = wallpaper.as<Scene> ();

    // setup the scene camera
    this->m_camera = std::make_unique<Camera> (*this, scene->camera);

    float width = scene->camera.projection.width;
    float height = scene->camera.projection.height;

    // detect size if the orthogonal project is auto
    if (scene->camera.projection.isAuto) {
	// TODO: CALCULATE ORTHOGONAL PROJECTION BASED ON CONTENT'S SIZE HERE
    }

    this->m_parallaxDisplacement = { 0, 0 };

    // TODO: CONVERSION
    this->m_camera->setOrthogonalProjection (width, height);

    // setup framebuffers here as they're required for the scene setup
    this->setupFramebuffers ();

    const uint32_t sceneWidth = this->m_camera->getWidth ();
    const uint32_t sceneHeight = this->m_camera->getHeight ();

    this->_rt_shadowAtlas = this->create (
	"_rt_shadowAtlas", TextureFormat_ARGB8888, TextureFlags_ClampUVs, 1.0, { sceneWidth, sceneHeight },
	{ sceneWidth, sceneHeight }
    );
    this->alias ("_alias_lightCookie", "_rt_shadowAtlas");

    // set clear color
    const glm::vec3 clearColor = scene->colors.clear->value->getVec3 ();

    glClearColor (clearColor.r, clearColor.g, clearColor.b, 1.0f);

    // how many lights of each kind the generated LightingV1 module handles;
    // has to be known before the first shader is compiled
    {
	Shaders::ShaderUnit::LightingLimits limits;

	for (const auto& object : scene->objects) {
	    if (!object->is<Light> ()) {
		continue;
	    }

	    const std::string& type = object->as<Light> ()->type;

	    if (type == "point" || type == "lpoint") {
		limits.point += 1;
	    } else if (type == "spot" || type == "lspot") {
		limits.spot += 1;
	    } else if (type == "directional") {
		limits.directional += 1;
	    }
	}

	limits.point = std::min (limits.point, static_cast<int> (MAX_LIGHTS_PER_KIND));
	limits.spot = std::min (limits.spot, static_cast<int> (MAX_LIGHTS_PER_KIND));
	limits.directional = std::min (limits.directional, static_cast<int> (MAX_LIGHTS_PER_KIND));

	this->m_pointLightCount = limits.point;
	this->m_spotLightCount = limits.spot;
	this->m_directionalLightCount = limits.directional;

	Shaders::ShaderUnit::setLightingLimits (limits);
    }

    // create all objects based off their dependencies
    for (const auto& object : scene->objects) {
	this->createObject (*object);
    }

    // copy over objects by render order
    for (const auto& object : scene->objects) {
	this->addObjectToRenderOrder (*object);
    }

    this->collectScriptedValues ();

    // create extra framebuffers for the bloom effect
    this->_rt_4FrameBuffer = this->create (
	"_rt_4FrameBuffer", TextureFormat_ARGB8888, TextureFlags_ClampUVs, 1.0, { sceneWidth / 4, sceneHeight / 4 },
	{ sceneWidth / 4, sceneHeight / 4 }
    );
    this->_rt_8FrameBuffer = this->create (
	"_rt_8FrameBuffer", TextureFormat_ARGB8888, TextureFlags_ClampUVs, 1.0, { sceneWidth / 8, sceneHeight / 8 },
	{ sceneWidth / 8, sceneHeight / 8 }
    );
    this->_rt_Bloom = this->create (
	"_rt_Bloom", TextureFormat_ARGB8888, TextureFlags_ClampUVs, 1.0, { sceneWidth / 8, sceneHeight / 8 },
	{ sceneWidth / 8, sceneHeight / 8 }
    );

    //
    // Had to get a little creative with the effects to achieve the same bloom effect without any custom code
    // this custom image loads some effect files from the virtual container to achieve the same bloom effect
    // this approach requires of two extra draw calls due to the way the effect works in official WPE
    // (it renders directly to the screen, whereas here we never do that from a scene)
    //

    const auto bloomOrigin = glm::vec3 { sceneWidth / 2, sceneHeight / 2, 0.0f };
    const auto bloomSize = glm::vec2 { sceneWidth, sceneHeight };

    const JSON bloom
	= { { "image", "models/wpenginelinux.json" },
	    { "name", "bloomimagewpenginelinux" },
	    { "visible", true },
	    { "scale", "1.0 1.0 1.0" },
	    { "angles", "0.0 0.0 0.0" },
	    { "origin",
	      std::to_string (bloomOrigin.x) + " " + std::to_string (bloomOrigin.y) + " "
		  + std::to_string (bloomOrigin.z) },
	    { "size", std::to_string (bloomSize.x) + " " + std::to_string (bloomSize.y) },
	    { "id", -1 },
	    { "effects",
	      JSON::array (
		  { { { "file", "effects/wpenginelinux/bloomeffect.json" },
		      { "id", 15242000 },
		      { "name", "" },
		      { "passes",
			JSON::array (
			    { { { "constantshadervalues",
				  { { "bloomstrength", this->getScene ().camera.bloom.strength->value->getFloat () },
				    { "bloomthreshold",
				      this->getScene ().camera.bloom.threshold->value->getFloat () } } } },
			      { { "constantshadervalues",
				  { { "bloomstrength", this->getScene ().camera.bloom.strength->value->getFloat () },
				    { "bloomthreshold",
				      this->getScene ().camera.bloom.threshold->value->getFloat () } } } },
			      { { "constantshadervalues",
				  { { "bloomstrength", this->getScene ().camera.bloom.strength->value->getFloat () },
				    { "bloomthreshold",
				      this->getScene ().camera.bloom.threshold->value->getFloat () } } } } }
			) } } }
	      ) } };

    // create image for bloom passes
    if (scene->camera.bloom.enabled->value->getBool ()) {
	this->m_bloomObjectData = ObjectParser::parse (bloom, scene->project);
	this->m_bloomObject = this->createObject (*this->m_bloomObjectData);

	this->m_objectsByRenderOrder.push_back (this->m_bloomObject);
    }
}

CScene::~CScene () {
    // bloom object is in the objects list, so no need to explicitly delete it
    this->m_bloomObject = nullptr;

    for (const auto& val : this->m_objects | std::views::values) {
	delete val;
    }

    this->m_objectsByRenderOrder.clear ();
    this->m_lights.clear ();
    this->m_objects.clear ();
}

Render::CObject* CScene::createObject (const Object& object) {
    Render::CObject* renderObject = nullptr;

    // ensure the item is not loaded already
    if (const auto current = this->m_objects.find (object.id); current != this->m_objects.end ()) {
	return current->second;
    }

    // check dependencies too!
    for (const auto& cur : object.dependencies) {
	// self-dependency is a possibility...
	if (cur == object.id) {
	    continue;
	}

	const auto dep
	    = std::ranges::find_if (this->getScene ().objects, [&cur] (const auto& o) { return o->id == cur; });

	if (dep != this->getScene ().objects.end ()) {
	    this->createObject (**dep);
	}
    }

    // check if the item has any parent and also create it first
    if (object.parent.has_value ()) {
	int parentId = object.parent.value ();

	const auto dep = std::ranges::find_if (this->getScene ().objects, [&parentId] (const auto& o) {
	    return o->id == parentId;
	});

	if (dep == this->getScene ().objects.end ()) {
	    sLog.exception ("Cannot find parent ", parentId, " for object ", object.id);
	}

	this->createObject (**dep);
    }

    if (object.is<Image> ()) {
	renderObject = createImageObject (*this, *object.as<Image> ());
    } else if (object.is<Sound> ()) {
	renderObject = new Objects::CSound (*this, *object.as<Sound> ());
    } else if (object.is<Text> ()) {
	renderObject = new Objects::CText (*this, *object.as<Text> ());
    } else if (object.is<Particle> ()) {
	renderObject = createParticleObject (*this, *object.as<Particle> ());
    } else if (object.is<Light> ()) {
	auto* light = new Objects::CLight (*this, *object.as<Light> ());

	this->m_lights.push_back (light);
	renderObject = light;
    } else {
	sLog.debug ("Unknown object type, creating placeholder, empty object: ", object.id);
	renderObject = new CObject (*this, object);
    }

    if (renderObject != nullptr) {
	this->m_objects.emplace (renderObject->getId (), renderObject);
    }

    return renderObject;
}

void CScene::addObjectToRenderOrder (const Object& object) {
    const auto obj = this->m_objects.find (object.id);

    // ignores not created objects like particle systems
    if (obj == this->m_objects.end ()) {
	return;
    }

    // take into account any dependency first
    for (const auto& dep : object.dependencies) {
	// self-dependency is possible
	if (dep == object.id) {
	    continue;
	}

	// add the dependency to the list if it's created
	auto depIt = std::ranges::find_if (this->getScene ().objects, [&dep] (const auto& o) { return o->id == dep; });

	if (depIt != this->getScene ().objects.end ()) {
	    this->addObjectToRenderOrder (**depIt);
	} else {
	    sLog.error ("Cannot find dependency ", dep, " for object ", object.id);
	}
    }

    // ensure we're added only once to the render list
    const auto renderIt = std::ranges::find_if (this->m_objectsByRenderOrder, [&object] (const auto& o) {
	return o->getId () == object.id;
    });

    if (renderIt == this->m_objectsByRenderOrder.end ()) {
	this->m_objectsByRenderOrder.emplace_back (obj->second);
    }
}

void CScene::registerScriptedValue (const UserSettingUniquePtr& setting) {
    if (!setting || !setting->value) {
	return;
    }

    auto* scripted = dynamic_cast<ScriptedDynamicValue*> (setting->value.get ());
    if (!scripted) {
	return;
    }

    if (std::ranges::find (this->m_scriptedValues, scripted) == this->m_scriptedValues.end ()) {
	this->m_scriptedValues.emplace_back (scripted);
    }
}

void CScene::collectScriptedValues () {
    this->m_scriptedValues.clear ();

    for (const auto& object : this->getScene ().objects) {
	this->registerScriptedValue (object->origin);
	this->registerScriptedValue (object->groupScale);
	this->registerScriptedValue (object->groupAngles);
	this->registerScriptedValue (object->groupVisible);

	if (object->is<Image> ()) {
	    const auto* image = object->as<Image> ();
	    this->registerScriptedValue (image->scale);
	    this->registerScriptedValue (image->angles);
	    this->registerScriptedValue (image->visible);
	    this->registerScriptedValue (image->alpha);
	    this->registerScriptedValue (image->color);
	    this->registerScriptedValue (image->parallaxDepth);

	    for (const auto& effect : image->effects) {
		this->registerScriptedValue (effect->visible);
		for (const auto& pass : effect->passOverrides) {
		    for (const auto& constant : pass->constants | std::views::values) {
			this->registerScriptedValue (constant);
		    }
		}
	    }
	} else if (object->is<Particle> ()) {
	    const auto* particle = object->as<Particle> ();
	    this->registerScriptedValue (particle->scale);
	    this->registerScriptedValue (particle->angles);
	    this->registerScriptedValue (particle->visible);
	    this->registerScriptedValue (particle->parallaxDepth);
	} else if (object->is<Light> ()) {
	    const auto* light = object->as<Light> ();
	    this->registerScriptedValue (light->color);
	    this->registerScriptedValue (light->intensity);
	    this->registerScriptedValue (light->radius);
	} else if (object->is<Text> ()) {
	    const auto* text = object->as<Text> ();
	    this->registerScriptedValue (text->text);
	    this->registerScriptedValue (text->visible);
	    this->registerScriptedValue (text->color);
	    this->registerScriptedValue (text->alpha);
	    this->registerScriptedValue (text->scale);
	    this->registerScriptedValue (text->angles);
	    this->registerScriptedValue (text->pointSize);
	}
    }
}

void CScene::updateScriptedValues () {
    for (const auto& scripted : this->m_scriptedValues) {
	scripted->reevaluate (this);
    }
}

int CScene::getPointLightCount () const { return this->m_pointLightCount; }

int CScene::getSpotLightCount () const { return this->m_spotLightCount; }

int CScene::getDirectionalLightCount () const { return this->m_directionalLightCount; }

const glm::vec4* CScene::getPointLightColors () const { return this->m_pointLightColors; }

const glm::vec4* CScene::getPointLightOrigins () const { return this->m_pointLightOrigins; }

const glm::vec4* CScene::getSpotLightColors () const { return this->m_spotLightColors; }

const glm::vec4* CScene::getSpotLightOrigins () const { return this->m_spotLightOrigins; }

const glm::vec4* CScene::getSpotLightDirections () const { return this->m_spotLightDirections; }

const glm::vec4* CScene::getSpotLightExponents () const { return this->m_spotLightExponents; }

const glm::vec4* CScene::getDirectionalLightColors () const { return this->m_directionalLightColors; }

const glm::vec4* CScene::getDirectionalLightDirections () const { return this->m_directionalLightDirections; }

const glm::vec3* CScene::getEyePosition () const { return &this->m_eyePosition; }

void CScene::updateLights () {
    for (auto& color : this->m_pointLightColors) color = glm::vec4 (0.0f);
    for (auto& origin : this->m_pointLightOrigins) origin = glm::vec4 (0.0f);
    for (auto& color : this->m_spotLightColors) color = glm::vec4 (0.0f);
    for (auto& origin : this->m_spotLightOrigins) origin = glm::vec4 (0.0f);
    for (auto& direction : this->m_spotLightDirections) direction = glm::vec4 (0.0f);
    for (auto& exponent : this->m_spotLightExponents) exponent = glm::vec4 (0.0f);
    for (auto& color : this->m_directionalLightColors) color = glm::vec4 (0.0f);
    for (auto& direction : this->m_directionalLightDirections) direction = glm::vec4 (0.0f);

    const auto& camera = this->getCamera ();

    this->m_eyePosition = camera.getEye ();

    glm::vec3 forward = camera.getCenter () - camera.getEye ();

    if (glm::length (forward) < 0.000001f) {
	forward = glm::vec3 (0.0f, 0.0f, -1.0f);
    }

    forward = glm::normalize (forward);

    struct Candidate {
	const Objects::CLight* light;
	glm::vec3 origin;
	float depth;
    };

    std::vector<Candidate> points;
    std::vector<Candidate> spots;
    std::vector<Candidate> directionals;

    // debugging aid: A/B a frame with and without its lights
    const bool lightsDisabled = std::getenv ("LWE_NO_LIGHTS") != nullptr;

    for (const auto* light : this->m_lights) {
	if (lightsDisabled || light == nullptr || !this->isVisibleInHierarchy (light->getLight ())) {
	    continue;
	}

	// an unlit light is not worth a slot
	if (light->getIntensity () <= 0.0f || light->getRadius () <= 0.0f) {
	    continue;
	}

	const std::string& type = light->getLight ().type;
	const glm::vec3 origin = light->getOrigin ();
	const Candidate candidate { light, origin, glm::dot (origin, forward) };

	if (type == "point" || type == "lpoint") {
	    points.push_back (candidate);
	} else if (type == "spot" || type == "lspot") {
	    spots.push_back (candidate);
	} else if (type == "directional") {
	    directionals.push_back (candidate);
	}
    }

    // Wallpaper Engine keeps the lights nearest the camera when a kind has
    // more lights than the shaders take
    const auto byDepth = [] (const Candidate& left, const Candidate& right) { return left.depth < right.depth; };

    std::stable_sort (points.begin (), points.end (), byDepth);
    std::stable_sort (spots.begin (), spots.end (), byDepth);
    std::stable_sort (directionals.begin (), directionals.end (), byDepth);

    const auto pointCount = std::min<std::size_t> (points.size (), this->m_pointLightCount);
    const auto spotCount = std::min<std::size_t> (spots.size (), this->m_spotLightCount);
    const auto directionalCount = std::min<std::size_t> (directionals.size (), this->m_directionalLightCount);

    for (std::size_t i = 0; i < pointCount; ++i) {
	const auto* light = points[i].light;

	this->m_pointLightColors[i]
	    = glm::vec4 (light->getColor () * light->getIntensity (), light->getRadius ());
	this->m_pointLightOrigins[i] = glm::vec4 (points[i].origin, light->getExponent ());
    }

    for (std::size_t i = 0; i < spotCount; ++i) {
	const auto* light = spots[i].light;

	this->m_spotLightColors[i] = glm::vec4 (light->getColor () * light->getIntensity (), light->getRadius ());
	// cone angles travel as their cosines, as Wallpaper Engine passes them
	this->m_spotLightOrigins[i]
	    = glm::vec4 (spots[i].origin, std::cos (glm::radians (light->getInnerCone ())));
	this->m_spotLightDirections[i]
	    = glm::vec4 (light->getDirection (), std::cos (glm::radians (light->getOuterCone ())));
	this->m_spotLightExponents[i] = glm::vec4 (light->getExponent (), 0.0f, 0.0f, 0.0f);
    }

    for (std::size_t i = 0; i < directionalCount; ++i) {
	const auto* light = directionals[i].light;

	this->m_directionalLightColors[i] = glm::vec4 (light->getColor () * light->getIntensity (), 1.0f);
	// towards the light: it shines along its own x axis
	this->m_directionalLightDirections[i] = glm::vec4 (-light->getDirection (), 0.0f);
    }

    if (std::getenv ("LWE_LIGHT_DEBUG") != nullptr) {
	sLog.out (
	    "Lights: ", this->m_lights.size (), " in scene; packed ", pointCount, " point, ", spotCount, " spot, ",
	    directionalCount, " directional"
	);
    }
}

Camera& CScene::getCamera () const { return *this->m_camera; }

CObject* CScene::objectAt (const glm::vec2& worldPosition) const {
    // last drawn wins: the render order is back-to-front
    for (auto it = this->m_objectsByRenderOrder.rbegin (); it != this->m_objectsByRenderOrder.rend (); ++it) {
	CObject* object = *it;

	// the bloom object is a synthetic full-screen layer, never a hit target
	if (object == nullptr || object == this->m_bloomObject
	    || !this->isVisibleInHierarchy (object->getObject ())) {
	    continue;
	}

	if (object->is<Objects::CImage> ()) {
	    if (object->as<Objects::CImage> ()->hitTest (worldPosition)) {
		return object;
	    }
	} else if (object->is<Objects::CText> ()) {
	    if (object->as<Objects::CText> ()->hitTest (worldPosition)) {
		return object;
	    }
	}
    }

    return nullptr;
}

void CScene::dispatchMouseEvents () {
    const auto& mouse = this->getContext ().getInputContext ().getMouseInput ();
    const glm::vec2 world = {
	this->m_mousePosition.x * static_cast<float> (this->getWidth ()),
	this->m_mousePosition.y * static_cast<float> (this->getHeight ()),
    };

    auto& scripts = WallpaperEngine::Scripting::ScriptEngine::instance ();

    // A script can live on a group while the pixels under the cursor belong to
    // one of its children, so the event goes to the hit object and then up the
    // whole parent chain. Modules that do not export the callback are skipped.
    std::vector<CObject*> chain = {};
    CObject* hovered = this->objectAt (world);

    for (CObject* current = hovered; current != nullptr;) {
	chain.push_back (current);

	const auto& data = current->getObject ();

	if (!data.parent.has_value ()) {
	    break;
	}

	current = const_cast<CObject*> (this->getObject (data.parent.value ()));
    }

    if (std::getenv ("LWE_CURSOR_DEBUG") != nullptr) {
	std::string chainText;

	for (const CObject* object : chain) {
	    chainText += std::to_string (object->getId ()) + " ";
	}

	sLog.out (
	    "Cursor: normalized=", this->m_mousePosition.x, ",", this->m_mousePosition.y,
	    " world=", world.x, ",", world.y,
	    " scene=", this->getWidth (), "x", this->getHeight (),
	    " chain=", chainText
	);
    }

    const auto dispatchTo = [&] (const char* functionName, const std::vector<CObject*>& objects) {
	for (CObject* object : objects) {
	    scripts.dispatchCursorEvent (
		functionName, *this, object->getId (), object->getObject ().name, world, this->m_mousePosition
	    );
	}
    };

    const bool chainChanged = chain != this->m_hoveredChain;

    if (chainChanged) {
	for (CObject* object : this->m_hoveredChain) {
	    if (std::ranges::find (chain, object) == chain.end ()) {
		dispatchTo ("cursorLeave", { object });
	    }
	}

	for (CObject* object : chain) {
	    if (std::ranges::find (this->m_hoveredChain, object) == this->m_hoveredChain.end ()) {
		dispatchTo ("cursorEnter", { object });
	    }
	}

	this->m_hoveredChain = chain;
    }

    if (!chain.empty () && world != this->m_mouseWorldLast) {
	dispatchTo ("cursorMove", chain);
    }

    const bool down = mouse.leftClick () == WallpaperEngine::Input::MouseClickStatus::Clicked
	|| std::getenv ("LWE_FAKE_CLICK") != nullptr;

    if (down != this->m_mouseDownLast) {
	if (!chain.empty ()) {
	    dispatchTo (down ? "cursorDown" : "cursorUp", chain);
	}

	this->m_mouseDownLast = down;
    }

    this->m_mouseWorldLast = world;
}

bool CScene::isVisibleInHierarchy (const Object& object) const {
    constexpr int kMaxParentDepth = 32;

    const Object* current = &object;

    for (int depth = 0; current != nullptr && depth < kMaxParentDepth; ++depth) {
	if (!ownVisibility (*current)) {
	    return false;
	}

	if (!current->parent.has_value ()) {
	    break;
	}

	const CObject* parent = this->getObject (current->parent.value ());
	current = parent != nullptr ? &parent->getObject () : nullptr;
    }

    return true;
}

bool CScene::isDependencySource (int objectId) const {
    for (const auto& object : this->getScene ().objects) {
	if (object->id == objectId) {
	    continue;
	}

	if (std::ranges::find (object->dependencies, objectId) != object->dependencies.end ()) {
	    return true;
	}
    }

    return false;
}

void CScene::renderFrame (const glm::ivec4& viewport) {
    // ensure the virtual mouse position is up to date
    this->updateMouse (viewport);

    auto profileStart = std::chrono::steady_clock::now ();

    this->updateScriptedValues ();

    auto profileAfterScripts = std::chrono::steady_clock::now ();

    // feed pointer changes to the scripts that own the affected objects; this
    // has to happen after the frame's globals (__layers, cursor position) exist
    this->dispatchMouseEvents ();

    this->updateLights ();

    // update the parallax position if required
    if (this->getScene ().camera.parallax.enabled->value->getBool ()
	&& !this->getContext ().getApp ().getContext ().settings.mouse.disableparallax) {
	const float influence = this->getScene ().camera.parallax.mouseInfluence->value->getFloat ();
	const float amount = this->getScene ().camera.parallax.amount->value->getFloat ();
	const float delay = glm::clamp (
	    this->getScene ().camera.parallax.delay->value->getFloat () * (g_Time - g_TimeLast), 0.0f, 1.0f
	);

	const glm::vec2 centeredMouse = this->m_mousePosition - glm::vec2 (0.5f, 0.5f);
	this->m_parallaxDisplacement
	    = glm::mix (this->m_parallaxDisplacement, (centeredMouse * amount) * influence, delay);
    }

    // update main textures for images
    for (const auto& cur : this->m_objectsByRenderOrder) {
	if (!cur->is<Objects::CImage> ()) {
	    continue;
	}

	const Objects::CImage* image = cur->as<Objects::CImage> ();

#if !NDEBUG
	const std::string message = "Updating texture " + image->getImage ().model->filename;

	glPushDebugGroup (GL_DEBUG_SOURCE_APPLICATION, 0, -1, message.c_str ());
#endif

	image->getTexture ()->update ();

#if !NDEBUG
	glPopDebugGroup ();
#endif
    }

    auto profileAfterTextures = std::chrono::steady_clock::now ();

    // bind the vertex array
    glBindVertexArray (this->m_vaoBuffer);
    // use the scene's framebuffer by default
    glBindFramebuffer (GL_FRAMEBUFFER, this->getWallpaperFramebuffer ());
    // ensure we render over the whole framebuffer
    glViewport (0, 0, this->m_sceneFBO->getRealWidth (), this->m_sceneFBO->getRealHeight ());

    glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    for (const auto& cur : this->m_objectsByRenderOrder) {
	const auto& debug = this->getContext ().getApp ().getContext ().settings.render.debug;
	if (debug.objectFilter.has_value () && cur->getId () != debug.objectFilter.value ()) {
	    continue;
	}
	if (std::ranges::find (debug.skipObjects, cur->getId ()) != debug.skipObjects.end ()) {
	    continue;
	}

	// a hidden object must not draw, but an object used as an effect source by
	// a visible one still has to run so its framebuffer is up to date
	const auto& object = cur->getObject ();

	if (!this->isVisibleInHierarchy (object) && !this->isDependencySource (object.id)) {
	    continue;
	}

	cur->render ();
    }

    auto profileEnd = std::chrono::steady_clock::now ();

    if (std::getenv ("LWE_PROFILE") != nullptr) {
	static auto windowStart = std::chrono::steady_clock::now ();
	static double scriptMs = 0.0;
	static double textureMs = 0.0;
	static double objectMs = 0.0;
	static int frames = 0;

	const auto toMs = [] (std::chrono::steady_clock::duration value) {
	    return std::chrono::duration<double, std::milli> (value).count ();
	};

	scriptMs += toMs (profileAfterScripts - profileStart);
	textureMs += toMs (profileAfterTextures - profileAfterScripts);
	objectMs += toMs (profileEnd - profileAfterTextures);
	++frames;

	if (profileEnd - windowStart >= std::chrono::seconds (1)) {
	    sLog.out (
		"Scene profile: ", frames, " frames, scripts ", static_cast<int> (scriptMs), "ms, textures ",
		static_cast<int> (textureMs), "ms, objects ", static_cast<int> (objectMs), "ms"
	    );

	    windowStart = profileEnd;
	    scriptMs = textureMs = objectMs = 0.0;
	    frames = 0;
	}
    }
}

void CScene::updateMouse (const glm::ivec4& viewport) {
    // update virtual mouse position first
    const glm::dvec2 position = this->getContext ().getInputContext ().getMouseInput ().position ();

    // rollover the position to the last
    this->m_mousePositionLast = this->m_mousePosition;

    // calculate the current position of the mouse in viewport space [0, 1]
    double mouseX = glm::clamp ((position.x - viewport.x) / viewport.z, 0.0, 1.0);
    // Normalize Y coordinate (OpenGL convention: 0=bottom, 1=top)
    // Particle code expects this convention: 0=bottom results in negative Y (down), 1=top results in positive Y (up)
    double normalizedMouseY = glm::clamp ((position.y - viewport.y) / viewport.w, 0.0, 1.0);

    // Account for UV cropping when using fill/fit scaling modes
    // The scene may be rendered larger than viewport and cropped via UVs
    const auto uvs = this->getState ().getTextureUVs ();

    // Map mouse position from viewport space to scene UV space
    // UVs define what portion of the scene texture is visible
    this->m_mousePositionNormalized.x = uvs.ustart + mouseX * (uvs.uend - uvs.ustart);
    this->m_mousePositionNormalized.y = uvs.vstart + normalizedMouseY * (uvs.vend - uvs.vstart);

    // Invert previous normalization of Y to match what the shader expects
    double mouseY = 1.0 - normalizedMouseY; 

    this->m_mousePosition.x = this->m_mousePositionNormalized.x;
    this->m_mousePosition.y = uvs.vstart + mouseY * (uvs.vend - uvs.vstart);

    // headless testing hook: pretend the pointer sits at a fixed spot
    if (const char* fake = std::getenv ("LWE_FAKE_CURSOR"); fake != nullptr) {
	float fakeX = 0.0f;
	float fakeY = 0.0f;

	if (std::sscanf (fake, "%f,%f", &fakeX, &fakeY) == 2) {
	    // optional: sweep the fake pointer so drag handlers can be exercised
	    if (std::getenv ("LWE_FAKE_CURSOR_ANIMATE") != nullptr) {
		fakeX += 0.12f * std::sin (g_Time * 1.5f);
	    }

	    this->m_mousePositionNormalized = { fakeX, fakeY };
	    this->m_mousePosition = { fakeX, fakeY };
	}
    }
}

const Scene& CScene::getScene () const { return *this->getWallpaperData ().as<Scene> (); }

int CScene::getWidth () const { return this->m_camera->getWidth (); }

int CScene::getHeight () const { return this->m_camera->getHeight (); }

const glm::vec2* CScene::getMousePosition () const { return &this->m_mousePosition; }

const glm::vec2* CScene::getMousePositionLast () const { return &this->m_mousePositionLast; }

const glm::vec2* CScene::getMousePositionNormalized () const { return &this->m_mousePositionNormalized; }

const glm::vec2* CScene::getParallaxDisplacement () const { return &this->m_parallaxDisplacement; }

const std::vector<CObject*>& CScene::getObjectsByRenderOrder () const { return this->m_objectsByRenderOrder; }

const CObject* CScene::getObject (int id) const {
    const auto object = this->m_objects.find (id);
    return object == this->m_objects.end () ? nullptr : object->second;
}
