// Unit tests for zones and the chase engine (no GPU needed).
//   chaser_tests <path to examples/example_3_screens.xml>
#include "ChaseEngine.h"
#include "Zones.h"
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace chaser;

static int failures = 0;
#define CHECK( c )                                                    \
	do                                                                \
	{                                                                 \
		if( !( c ) )                                                  \
		{                                                             \
			std::printf( "FAIL %s:%d  %s\n", __FILE__, __LINE__, #c ); \
			++failures;                                               \
		}                                                             \
	} while( 0 )

static std::string LitString( Pattern p, int n, long long s, int w )
{
	std::string r;
	for( int i = 0; i < n; ++i )
		r += ChaseEngine::IsLit( p, i, n, s, w ) ? '#' : '.';
	return r;
}

static std::string Levels( const std::vector< float >& v )
{
	std::string r;
	for( float x : v )
		r += x > 0.99f ? '#' : x > 0.01f ? '+' : '.';
	return r;
}

int main( int argc, char** argv )
{
	// --- patterns
	CHECK( LitString( Pattern::ChaseForward, 5, 0, 1 ) == "#...." );
	CHECK( LitString( Pattern::ChaseForward, 5, 6, 2 ) == ".##.." );
	CHECK( LitString( Pattern::ChaseForward, 5, 4, 2 ) == "#...#" );
	CHECK( LitString( Pattern::ChaseBackward, 5, 0, 1 ) == "....#" );
	CHECK( LitString( Pattern::ChaseBackward, 5, 1, 1 ) == "...#." );
	{
		std::string seq;
		for( int s = 0; s < 8; ++s )
			seq += LitString( Pattern::PingPong, 4, s, 1 ) + " ";
		CHECK( seq == "#... .#.. ..#. ...# ..#. .#.. #... .#.. " );
	}
	CHECK( LitString( Pattern::AllStrobe, 4, 3, 1 ) == "####" );
	CHECK( LitString( Pattern::OddEven, 5, 0, 1 ) == "#.#.#" );
	CHECK( LitString( Pattern::OddEven, 5, 1, 1 ) == ".#.#." );
	CHECK( LitString( Pattern::CenterOut, 5, 0, 1 ) == "..#.." );
	CHECK( LitString( Pattern::CenterOut, 5, 1, 1 ) == ".#.#." );
	CHECK( LitString( Pattern::CenterOut, 5, 2, 1 ) == "#...#" );
	CHECK( LitString( Pattern::CenterOut, 4, 0, 1 ) == ".##." );
	CHECK( LitString( Pattern::Fill, 4, 2, 1 ) == "###." );

	// --- free timing: 4 Hz, duty 50 %
	{
		ChaseEngine e;
		e.SetZoneCount( 4 );
		EngineSettings s;
		s.rateHz = 4;
		s.duty   = 0.5f;
		FrameInput in;
		in.dt = 0.0;
		CHECK( Levels( e.Update( s, in ) ) == "#..." );
		in.dt = 0.15;// 0.6 of a step -> gate closed
		CHECK( Levels( e.Update( s, in ) ) == "...." );
		in.dt = 0.15;// step 1, phase 0.2
		CHECK( Levels( e.Update( s, in ) ) == ".#.." );
		CHECK( e.Step() == 1 );
	}

	// --- decay leaves a tail
	{
		ChaseEngine e;
		e.SetZoneCount( 3 );
		EngineSettings s;
		s.rateHz = 10;
		s.duty   = 1.0f;
		s.decay  = 0.7f;
		FrameInput in;
		in.dt = 0.0;
		e.Update( s, in );
		in.dt = 0.11;
		CHECK( Levels( e.Update( s, in ) ) == "+#." );
	}

	// --- BPM sync: 1 step per beat
	{
		ChaseEngine e;
		e.SetZoneCount( 4 );
		EngineSettings s;
		s.trigger      = Trigger::Bpm;
		s.beatsPerStep = 1.0f;
		s.duty         = 1.0f;
		FrameInput in;
		in.dt       = 0.01;
		in.barPhase = 0.10f;
		e.Update( s, in );
		in.barPhase = 0.30f;// beat 1
		CHECK( Levels( e.Update( s, in ) ) == ".#.." );
		in.barPhase = 0.80f;// beat 3
		CHECK( Levels( e.Update( s, in ) ) == "...#" );
		in.barPhase = 0.05f;// next bar, beat 0 -> 4 steps total
		CHECK( Levels( e.Update( s, in ) ) == "#..." );
		CHECK( e.Step() == 4 );
	}

	// --- manual steps
	{
		ChaseEngine e;
		e.SetZoneCount( 3 );
		EngineSettings s;
		s.trigger = Trigger::Manual;
		s.duty    = 1.0f;
		FrameInput in;
		in.dt          = 0.02;
		in.manualSteps = 1;
		CHECK( Levels( e.Update( s, in ) ) == ".#." );
		in.manualSteps = 0;
		CHECK( Levels( e.Update( s, in ) ) == ".#." );
		in.reset = true;
		CHECK( Levels( e.Update( s, in ) ) == "#.." );
	}

	// --- audio: synthetic kick drum every 0.5 s in the bass band
	{
		ChaseEngine e;
		e.SetZoneCount( 4 );
		EngineSettings s;
		s.trigger = Trigger::Audio;
		s.band    = Band::Bass;
		s.duty    = 0.3f;
		std::vector< float > fft( 2048, 0.001f );
		FrameInput in;
		in.dt         = 1.0 / 60.0;
		in.fft        = fft.data();
		in.fftSize    = fft.size();
		in.sampleRate = 44100;
		int hits      = 0;
		for( int f = 0; f < 60 * 4; ++f )// 4 seconds
		{
			const double t   = f / 60.0;
			const double ph  = std::fmod( t, 0.5 );
			const float kick = ph < 0.08 ? 0.6f * (float)std::exp( -ph * 30 ) : 0.0f;
			for( int b = 2; b < 14; ++b )// ~40-300 Hz
				fft[ b ] = 0.01f + kick;
			for( int b = 200; b < 400; ++b )// constant hi-hat noise in the highs (must not trigger)
				fft[ b ] = 0.05f;
			e.Update( s, in );
			hits += e.Audio().hit ? 1 : 0;
		}
		std::printf( "audio hits detected: %d (expected 7-8: the first kick lands on the warm-up frame)\n", hits );
		CHECK( hits >= 7 && hits <= 8 );
		CHECK( e.Step() == hits );

		// High band only sees constant noise -> no hits
		ChaseEngine h;
		h.SetZoneCount( 4 );
		s.band = Band::High;
		int highHits = 0;
		for( int f = 0; f < 120; ++f )
		{
			h.Update( s, in );
			highHits += h.Audio().hit ? 1 : 0;
		}
		CHECK( highHits == 0 );
	}

	// --- audio meter fills zones by level
	{
		ChaseEngine e;
		e.SetZoneCount( 5 );
		EngineSettings s;
		s.pattern = Pattern::AudioMeter;
		std::vector< float > fft( 512, 0.0f );
		FrameInput in;
		in.dt      = 1.0 / 60.0;
		in.fft     = fft.data();
		in.fftSize = fft.size();
		for( float& v : fft )
			v = 0.5f;
		std::string full = Levels( e.Update( s, in ) );
		CHECK( full == "#####" );// first frame sets the auto-gain peak
		for( float& v : fft )
			v = 0.5f * 0.5f;// half level
		for( int i = 0; i < 30; ++i )// 0.5 s: smoothing settles, auto-gain has drifted only a little
			e.Update( s, in );
		std::string half = Levels( e.Update( s, in ) );
		std::printf( "meter at half level: %s\n", half.c_str() );
		CHECK( half == "##+.." );
	}

	// --- random never repeats the same zone twice in a row
	{
		ChaseEngine e;
		e.SetZoneCount( 6 );
		EngineSettings s;
		s.pattern = Pattern::Random;
		s.trigger = Trigger::Manual;
		s.duty    = 1.0f;
		FrameInput in;
		in.dt = 0.02;
		std::string prev;
		bool repeat = false;
		for( int i = 0; i < 50; ++i )
		{
			in.manualSteps = 1;
			std::string cur = Levels( e.Update( s, in ) );
			if( cur == prev )
				repeat = true;
			prev = cur;
		}
		CHECK( !repeat );
	}

	// --- grid + selection + order + raster
	{
		ZoneSet g = MakeGrid( 3, 2 );
		CHECK( g.zones.size() == 6 );
		auto sel = ParseSelection( "", 6 );
		CHECK( sel == std::vector< bool >( 6, true ) );
		sel = ParseSelection( "1-2, 5", 6 );
		CHECK( sel == ( std::vector< bool >{ true, true, false, false, true, false } ) );
		sel = ParseSelection( "4-", 6 );
		CHECK( sel == ( std::vector< bool >{ false, false, false, true, true, true } ) );
		auto all = ParseSelection( "", 6 );
		auto lr  = OrderZones( g, all, Order::LeftToRight, 1 );
		CHECK( lr == ( std::vector< int >{ 0, 3, 1, 4, 2, 5 } ) );
		auto map = RasterizeZoneMap( g, all, 30, 20 );
		// bottom-up rows: row 0 is the bottom row -> zones 4..6
		CHECK( map[ 0 ] == 4 && map[ 29 ] == 6 );
		CHECK( map[ 19 * 30 + 0 ] == 1 && map[ 19 * 30 + 29 ] == 3 );
		CHECK( map[ 19 * 30 + 10 ] == 2 && map[ 19 * 30 + 9 ] == 1 );
	}

	// --- Advanced Output XML
	if( argc > 1 )
	{
		ZoneSet s = LoadAdvancedOutput( argv[ 1 ], GroupBy::Slices );
		std::printf( "xml: %s %s\n", s.info.c_str(), s.error.c_str() );
		CHECK( s.error.empty() );
		CHECK( s.compWidth == 3840 && s.compHeight == 1080 );
		CHECK( s.zones.size() == 4 );// mask and disabled slice skipped
		if( s.zones.size() == 4 )
		{
			CHECK( s.zones[ 0 ].name == "LED Left / Left A" );
			CHECK( s.zones[ 3 ].name == "LED Right & Wing / Right (tilted)" );
			CHECK( std::fabs( s.zones[ 1 ].cx - 0.375 ) < 1e-9 );
		}
		ZoneSet byScreen = LoadAdvancedOutput( argv[ 1 ], GroupBy::Screens );
		CHECK( byScreen.zones.size() == 3 );
		if( byScreen.zones.size() == 3 )
			CHECK( byScreen.zones[ 0 ].polys.size() == 2 );
		auto map = RasterizeZoneMap( byScreen, ParseSelection( "", 3 ), 384, 108 );
		const int top = 107 * 384;// top row (bottom-up storage)
		CHECK( map[ top + 10 ] == 1 && map[ top + 150 ] == 1 && map[ top + 200 ] == 2 );
		CHECK( map[ 50 * 384 + 330 ] == 3 );// inside the tilted quad
		CHECK( map[ 107 * 384 + 383 ] == 0 );// outside every slice
		ZoneSet bad = ParseAdvancedOutput( "<hello/>", GroupBy::Slices );
		CHECK( !bad.error.empty() );
	}
	else
	{
		std::printf( "(xml test skipped: pass examples/example_3_screens.xml)\n" );
	}

	std::printf( failures ? "%d FAILED\n" : "all tests passed\n", failures );
	return failures ? 1 : 0;
}
