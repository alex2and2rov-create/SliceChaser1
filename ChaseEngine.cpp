#include "ChaseEngine.h"
#include <algorithm>
#include <cmath>

namespace chaser
{
namespace
{
long long Mod( long long a, long long n )
{
	long long r = a % n;
	return r < 0 ? r + n : r;
}
}// namespace

void ChaseEngine::SetZoneCount( int n )
{
	n = std::max( 0, n );
	if( (int)levels.size() == n )
		return;
	levels.assign( n, 0.0f );
	output.assign( n, 0.0f );
	randomLit.assign( n, 0 );
}

bool ChaseEngine::IsLit( Pattern p, int pos, int n, long long s, int width )
{
	if( n <= 0 )
		return false;
	width = std::max( 1, std::min( n, width ) );
	switch( p )
	{
	case Pattern::ChaseForward: return Mod( pos - s, n ) < width;
	case Pattern::ChaseBackward: return Mod( ( n - 1 - pos ) - s, n ) < width;
	case Pattern::PingPong:
	{
		if( n == 1 )
			return true;
		const long long period = 2LL * n - 2;
		const long long m      = Mod( s, period );
		const long long head   = m < n ? m : period - m;
		const bool forward     = m < n - 1 || m == 0;
		// the lit group trails behind the head in the direction of travel
		return forward ? ( pos <= head && pos > head - width ) : ( pos >= head && pos < head + width );
	}
	case Pattern::AllStrobe: return true;
	case Pattern::OddEven: return ( pos % 2 ) == (int)Mod( s, 2 );
	case Pattern::CenterOut:
	{
		const double center = ( n - 1 ) * 0.5;
		const int ring      = (int)std::floor( std::fabs( pos - center ) );
		const int rings     = (int)std::floor( center ) + 1;
		const int active    = (int)Mod( s, rings );
		return ring >= active && ring < active + width;
	}
	case Pattern::Fill: return pos <= Mod( s, n );
	default: return false;
	}
}

void ChaseEngine::Advance( long long newStep )
{
	if( newStep == step )
		return;
	step      = newStep;
	sinceStep = 0.0;
}

void ChaseEngine::AnalyseAudio( const EngineSettings& s, const FrameInput& in )
{
	audio.hit = false;
	sinceHit += in.dt;
	if( !in.fft || in.fftSize < 8 )
	{
		audio.level = 0.0f;
		return;
	}
	// Bins are linear from 0 Hz to Nyquist.
	const double nyquist = std::max( 8000u, in.sampleRate ) * 0.5;
	double lo = 20, hi = 16000;
	switch( s.band )
	{
	case Band::Bass: lo = 30, hi = 150; break;
	case Band::Mid: lo = 150, hi = 2000; break;
	case Band::High: lo = 2000, hi = 12000; break;
	default: break;
	}
	size_t b0 = (size_t)std::floor( lo / nyquist * in.fftSize );
	size_t b1 = (size_t)std::ceil( hi / nyquist * in.fftSize );
	b0        = std::min( b0, in.fftSize - 1 );
	b1        = std::max( b0 + 1, std::min( b1, in.fftSize ) );
	double e  = 0.0;
	for( size_t i = b0; i < b1; ++i )
		e += (double)in.fft[ i ] * in.fft[ i ];
	e /= (double)( b1 - b0 );

	// Auto gain: follow the loudest recent level, falling slowly (~4 s).
	const double rms = std::sqrt( e );
	peak             = std::max( rms, peak * std::exp( -in.dt / 4.0 ) );
	peak             = std::max( peak, 1e-6 );
	const float lvl  = (float)std::min( 1.0, rms / peak );
	// fast attack, ~150 ms release
	smoothLevel = lvl > smoothLevel ? lvl : smoothLevel + ( lvl - smoothLevel ) * (float)( 1.0 - std::exp( -in.dt / 0.15 ) );
	audio.level = smoothLevel;

	// First frame: just learn the level, never fire a hit on start-up.
	if( slowAvg <= 0.0 )
	{
		slowAvg = std::max( e, 1e-12 );
		lastE   = e;
		return;
	}

	// Hit detection: energy jumps above its own running average.
	const double k = 0.2 + ( 1.0 - std::max( 0.0f, std::min( 1.0f, s.sensitivity ) ) ) * 2.8;
	const bool loudEnough = rms > peak * 0.15;// ignore the noise floor
	if( !armed && e < slowAvg * ( 1.0 + k * 0.5 ) )
		armed = true;
	if( armed && loudEnough && e > slowAvg * ( 1.0 + k ) && e > lastE && sinceHit > 0.09 )
	{
		audio.hit = true;
		armed     = false;
		sinceHit  = 0.0;
	}
	slowAvg = std::max( 1e-12, slowAvg + ( e - slowAvg ) * ( 1.0 - std::exp( -in.dt / 0.4 ) ) );
	lastE   = e;
}

const std::vector< float >& ChaseEngine::Update( const EngineSettings& s, const FrameInput& in )
{
	const int n   = (int)levels.size();
	const double dt = std::max( 0.0, std::min( 1.0, in.dt ) );
	clock += dt;
	sinceStep += dt;

	AnalyseAudio( s, in );

	if( in.reset )
	{
		step       = 0;
		sinceStep  = 0.0;
		phaseAccum = 0.0;
		bpmStarted = false;
	}

	// --- timing: work out the current step and the phase within it
	double phase      = 0.0;
	const long long before = step;
	switch( s.trigger )
	{
	case Trigger::Free:
	{
		phaseAccum += dt * std::max( 0.01f, s.rateHz );
		const long long whole = (long long)std::floor( phaseAccum );
		if( whole > 0 )
		{
			phaseAccum -= (double)whole;
			Advance( step + whole );
		}
		phase = phaseAccum;
		break;
	}
	case Trigger::Bpm:
	{
		if( lastBarPhase >= 0.0f && in.barPhase < lastBarPhase - 0.5f )
			bars += 1.0;// bar wrapped
		lastBarPhase       = in.barPhase;
		const double beats = ( bars + in.barPhase ) * 4.0;
		const double per   = std::max( 1.0 / 64.0, (double)s.beatsPerStep );
		const long long bs = (long long)std::floor( beats / per + 1e-6 );
		if( !bpmStarted || in.reset )
		{
			lastBpmStep = bs;
			bpmStarted  = true;
		}
		if( bs != lastBpmStep )
		{
			Advance( step + ( bs - lastBpmStep ) );
			lastBpmStep = bs;
		}
		phase = beats / per - std::floor( beats / per + 1e-6 );
		break;
	}
	case Trigger::Audio:
	case Trigger::Manual:
	{
		const int events = s.trigger == Trigger::Audio ? ( audio.hit ? 1 : 0 ) : in.manualSteps;
		if( events > 0 )
		{
			// learn the typical gap between hits so "Duty" means the same thing at any tempo
			if( sinceStep > 0.03 && sinceStep < 3.0 )
				interval += ( std::min( 2.0, std::max( 0.06, sinceStep ) ) - interval ) * 0.35;
			Advance( step + events );
		}
		phase = std::min( 1.0, sinceStep / std::max( 0.05, interval ) );
		break;
	}
	}

	// New step for the random pattern: pick `width` distinct zones, avoiding a repeat.
	if( s.pattern == Pattern::Random && n > 0 && ( step != before || in.reset ) )
	{
		std::vector< char > prev = randomLit;
		std::fill( randomLit.begin(), randomLit.end(), 0 );
		const int w = std::max( 1, std::min( n, s.width ) );
		std::vector< int > pool;
		for( int i = 0; i < n; ++i )
			if( !prev[ i ] || n <= w )
				pool.push_back( i );
		if( (int)pool.size() < w )
			for( int i = 0; i < n; ++i )
				if( prev[ i ] )
					pool.push_back( i );
		std::shuffle( pool.begin(), pool.end(), rng );
		for( int i = 0; i < w && i < (int)pool.size(); ++i )
			randomLit[ pool[ i ] ] = 1;
	}

	// --- per-zone envelope
	const bool gateOpen = s.duty >= 0.999f || phase < s.duty;
	const double tau    = 0.6 * (double)s.decay * s.decay;// seconds
	const float release = tau <= 1e-4 ? 0.0f : (float)std::exp( -dt / tau );

	for( int pos = 0; pos < n; ++pos )
	{
		float target = 0.0f;
		if( s.pattern == Pattern::AudioMeter )
		{
			// level fills the zones in order; the last one partially
			const float fill = audio.level * n;
			target           = std::max( 0.0f, std::min( 1.0f, fill - pos ) );
		}
		else
		{
			const bool lit = s.pattern == Pattern::Random ? randomLit[ pos ] != 0 : IsLit( s.pattern, pos, n, step, s.width );
			target         = ( lit && gateOpen ) ? 1.0f : 0.0f;
		}
		levels[ pos ] = target >= levels[ pos ] ? target : std::max( target, levels[ pos ] * release );
	}

	// Inner strobe: lit zones flicker at innerStrobeHz (50 % duty).
	float flicker = 1.0f;
	if( s.innerStrobeHz > 0.01f )
		flicker = std::fmod( clock * s.innerStrobeHz, 1.0 ) < 0.5 ? 1.0f : 0.0f;
	for( int pos = 0; pos < n; ++pos )
		output[ pos ] = levels[ pos ] * flicker;
	return output;
}

}// namespace chaser
