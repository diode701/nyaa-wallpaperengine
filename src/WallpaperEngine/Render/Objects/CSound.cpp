#include <SDL.h>

#include <random>

#include "CSound.h"

#include "WallpaperEngine/FileSystem/Container.h"

using namespace WallpaperEngine::Render::Objects;

CSound::CSound (Wallpapers::CScene& scene, const Sound& sound) : CObject (scene, sound), m_sound (sound) {
    if (this->getContext ().getApp ().getContext ().settings.audio.enabled) {
	this->load ();
    }
}

CSound::~CSound () {
    // free all the sound buffers and streams
    for (const auto& stream : this->m_audioStreams) {
	this->getScene ().getAudioContext ().removeStream (stream.first);
	delete stream.second;
    }

    this->m_audioStreams.clear ();
}

void CSound::load () {
    std::vector<std::string> toLoad = this->m_sound.sounds;

    // "random" means one sound at a time, not the whole folder mixed together
    if (this->m_sound.playbackmode.has_value () && *this->m_sound.playbackmode == "random" && toLoad.size () > 1) {
	static std::mt19937 generator { std::random_device {} () };
	std::uniform_int_distribution<size_t> distribution (0, toLoad.size () - 1);

	toLoad = { toLoad[distribution (generator)] };
    }

    for (const auto& cur : toLoad) {
	auto stream
	    = new Audio::AudioStream (this->getScene ().getAudioContext (), this->getAssetLocator ().read (cur));

	stream->setRepeat (this->m_sound.playbackmode.has_value () && *this->m_sound.playbackmode == "loop");

	// add the stream to the context so it can be played
	this->m_audioStreams.insert_or_assign (this->getScene ().getAudioContext ().addStream (stream), stream);
    }
}

void CSound::render () {
    // the volume is a user setting and can be animated by a script
    if (!this->m_sound.volume || !this->m_sound.volume->value) {
	return;
    }

    const float volume = this->m_sound.volume->value->getFloat ();

    for (const auto& [streamId, stream] : this->m_audioStreams) {
	stream->setVolume (volume);
    }
}


