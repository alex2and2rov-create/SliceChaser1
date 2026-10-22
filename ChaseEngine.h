#pragma once
// Chase engine: decides, every frame, how bright each zone is.
// Pure logic (no OpenGL) so it can be unit-tested.

#include <cstdint>
#include <random>
#include <vector>

namespace chaser
{
enum class Pattern
{
	ChaseForward,
	ChaseBackward,
	PingPong,
	Random,
	AllStrobe,
	OddEven,
	CenterOut,
	Fill,
	AudioMeter
};

enum class Trigger
{
	Free, // steps per second
	Bpm,  // synced to the host's BPM / bar phase
	Audio,// one step per detected hit
	Manual// one step per "Step" press (MIDI / OSC / keyboard)
};

enum class Band
{
	Full,
	Bass,
	Mid,
	High
};

struct EngineSettings
{
	Pattern pattern     = Pattern::ChaseForward;
	Trigger trigger     = Trigger::Free;
	int width           = 1;     // zones lit at once
	float rateHz        = 4.0f;  // Free
	float beatsPerStep  = 1.0f;  // Bpm
	float duty          = 0.5f;  // part of each step that a zone is on (1 = hold)
	float decay         = 0.0f;  // 0 = hard cut, 1 = long tail
	float innerStrobeHz = 0.0f;  // flicker of lit zones, 0 = off
	Band band           = Band::Bass;
	float sensitivity   = 0.6f;  // audio hit detection
};

struct FrameInput
{
	double dt         = 0.0;// seconds since last frame
	float bpm         = 120.0f;
	float barPhase    = 0.0f;// 0..1 within the bar
	const float* fft  = nullptr;
	size_t fftSize    = 0;
	unsigned sampleRate = 44100;
	int manualSteps   = 0;    // "Step" presses since last frame
	bool reset        = false;// back to the first zone
};

struct AudioState
{
	float level = 0.0f;// 0..1, auto-gained band level
	bool hit    = false;// a hit was detected this frame
};

class ChaseEngine
{
public:
	// Number of zones taking part, in chase order.
	void SetZoneCount( int n );
	int ZoneCount() const { return (int)levels.size(); }

	// Advances time and returns per-position brightness 0..1 (index = position in chase order).
	const std::vector< float >& Update( const EngineSettings& s, const FrameInput& in );

	const AudioState& Audio() const { return audio; }
	long long Step() const { return step; }

	// Exposed for tests.
	static bool IsLit( Pattern p, int pos, int n, long long step, int width );

private:
	void AnalyseAudio( const EngineSettings& s, const FrameInput& in );
	void Advance( long long newStep );

	std::vector< float > levels;
	std::vector< float > output;
	std::vector< char > randomLit;

	long long step     = 0;
	double phaseAccum  = 0.0;// Free mode
	double sinceStep   = 0.0;// seconds since the last step
	double interval    = 0.5;// estimated seconds between event-driven steps
	double clock       = 0.0;// for the inner strobe

	// BPM tracking
	double bars        = 0.0;
	float lastBarPhase = -1.0f;
	long long lastBpmStep = 0;
	bool bpmStarted    = false;

	// Audio
	AudioState audio;
	double slowAvg  = 0.0;
	double lastE    = 0.0;
	double peak     = 1e-9;
	double sinceHit = 10.0;
	bool armed      = true;
	float smoothLevel = 0.0f;

	std::mt19937 rng{ 12345 };
};

}// namespace chaser
