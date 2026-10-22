#include "FFGLChaser.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>

using namespace ffglex;
using namespace chaser;

enum ParamID : FFUInt32
{
	// Zones
	PT_ZONE_SOURCE,
	PT_COLUMNS,
	PT_ROWS,
	PT_XML_FILE,
	PT_GROUP,
	PT_USE,
	PT_ORDER,
	PT_SHOW_ZONES,
	// Chase
	PT_PATTERN,
	PT_WIDTH,
	PT_TRIGGER,
	PT_RATE,
	PT_DIVISION,
	PT_STEP,
	PT_RESET,
	PT_BAND,
	PT_SENS,
	PT_AUDIO,// FFT buffer from the host
	// Strobe
	PT_DUTY,
	PT_DECAY,
	PT_INNER,
	PT_ON_LOOK,
	PT_COLOR_H,
	PT_COLOR_S,
	PT_COLOR_B,
	PT_COLOR_A,
	PT_OFF_LEVEL,
	PT_OUTSIDE,
	PT_COUNT
};

static const struct
{
	const char* name;
	float beats;
} kDivisions[] = { { "4 Bars", 16.0f }, { "2 Bars", 8.0f }, { "1 Bar", 4.0f }, { "1/2", 2.0f }, { "1/4 (beat)", 1.0f },
	               { "1/8", 0.5f },     { "1/16", 0.25f },  { "1/32", 0.125f }, { "1/4 Triplet", 2.0f / 3.0f }, { "1/8 Triplet", 1.0f / 3.0f } };
static const int kNumDivisions = sizeof( kDivisions ) / sizeof( kDivisions[ 0 ] );
static const unsigned kFftSize = 2048;

static CFFGLPluginInfo PluginInfo(
	PluginFactory< FFGLChaser >,
	"SLCH",                                                         // Plugin unique ID
	"Slice Chaser",                                                 // Plugin name
	2,                                                              // API major version
	1,                                                              // API minor version
	1,                                                              // Plugin major version
	0,                                                              // Plugin minor version
	FF_EFFECT,                                                      // Plugin type
	"Strobe / chase over slices, screens or a grid. Free, BPM, audio or manual.",
	"Slice Chaser for Resolume (FFGL)" );

static const char vertexShaderCode[] = R"(#version 410 core
layout( location = 0 ) in vec4 vPosition;
layout( location = 1 ) in vec2 vUV;
out vec2 uv;
void main()
{
	gl_Position = vPosition;
	uv = vUV;
}
)";

static const char fragmentShaderCode[] = R"(#version 410 core
uniform sampler2D inputTexture;
uniform vec2  maxUV;
uniform sampler2D zoneMap;     // r = zone id / 255, g = zone number label
uniform vec4  levels[ 64 ];    // brightness of zone k at levels[k/4][k%4]
uniform int   onLook;          // 0 content, 1 boost, 2 white, 3 colour, 4 invert
uniform vec4  flashColor;      // rgb + amount
uniform float offLevel;
uniform int   outside;         // 0 unchanged, 1 hidden, 2 like off zones
uniform int   showZones;
uniform float meterLevel;      // < 0: no meter
uniform float meterHit;
uniform vec2  resolution;

in vec2 uv;
out vec4 fragColor;

int zoneAt( ivec2 p )
{
	ivec2 s = textureSize( zoneMap, 0 );
	p = clamp( p, ivec2( 0 ), s - 1 );
	return int( texelFetch( zoneMap, p, 0 ).r * 255.0 + 0.5 );
}

vec3 hue( float h )
{
	return clamp( abs( mod( h * 6.0 + vec3( 0.0, 4.0, 2.0 ), 6.0 ) - 3.0 ) - 1.0, 0.0, 1.0 );
}

void main()
{
	vec4 c = texture( inputTexture, uv * maxUV );
	ivec2 s  = textureSize( zoneMap, 0 );
	ivec2 mp = ivec2( uv * vec2( s ) );
	int id   = zoneAt( mp );

	vec4 result;
	float lev = 0.0;
	if( id == 0 )
	{
		result = outside == 0 ? c : outside == 1 ? vec4( 0.0 ) : c * offLevel;
	}
	else
	{
		int k = id - 1;
		lev   = levels[ k / 4 ][ k % 4 ];
		vec4 on;
		if( onLook == 1 )
			on = vec4( min( c.rgb * 1.6 + 0.12 * c.a, vec3( 1.0 ) ), c.a );
		else if( onLook == 2 )
			on = vec4( 1.0 );
		else if( onLook == 3 )
			on = mix( c, vec4( flashColor.rgb, 1.0 ), flashColor.a );
		else if( onLook == 4 )
			on = vec4( c.a - c.rgb, c.a );
		else
			on = c;
		vec4 off = c * offLevel;
		result   = mix( off, on, lev );
	}

	if( showZones != 0 )
	{
		// tint every zone with its own colour, outline the borders, print the zone number
		if( id > 0 )
		{
			vec3 tint = hue( fract( float( id ) * 0.618034 ) );
			result.rgb = mix( result.rgb, tint, 0.35 );
			result.a   = max( result.a, 0.6 );
		}
		int w = max( 1, int( float( s.x ) / 900.0 ) );
		bool edge = zoneAt( mp + ivec2( w, 0 ) ) != id || zoneAt( mp - ivec2( w, 0 ) ) != id ||
		            zoneAt( mp + ivec2( 0, w ) ) != id || zoneAt( mp - ivec2( 0, w ) ) != id;
		if( edge && id > 0 )
			result = vec4( 1.0 );
		float label = texelFetch( zoneMap, mp, 0 ).g;
		if( label > 0.25 )
			result = vec4( label > 0.75 ? vec3( 1.0 ) : vec3( 0.0 ), 1.0 );

		// audio meter in the bottom-left corner
		if( meterLevel >= 0.0 )
		{
			vec2 p  = uv * resolution;
			vec2 m0 = vec2( 0.02, 0.03 ) * resolution;
			vec2 ms = vec2( 0.25, 0.025 ) * resolution;
			vec2 q  = ( p - m0 ) / ms;
			if( q.x >= 0.0 && q.y >= 0.0 && q.x <= 1.0 && q.y <= 1.0 )
				result = q.x <= meterLevel ? vec4( mix( vec3( 0.2, 1.0, 0.3 ), vec3( 1.0, 0.15, 0.1 ), meterHit ), 1.0 )
				                           : vec4( 0.1, 0.1, 0.1, 1.0 );
		}
	}
	fragColor = result;
}
)";

// 3x5 pixel digits for the zone numbers in "Show Zones"
static const uint16_t kDigits[ 10 ] = { 0x7B6F, 0x2C97, 0x73E7, 0x73CF, 0x5BC9, 0x79CF, 0x79EF, 0x7249, 0x7BEF, 0x7BCF };

FFGLChaser::FFGLChaser()
{
	SetMinInputs( 1 );
	SetMaxInputs( 1 );

	// --- Zones
	SetOptionParamInfo( PT_ZONE_SOURCE, "Zones", 2, 0.0f );
	SetParamElementInfo( PT_ZONE_SOURCE, 0, "Grid", 0.0f );
	SetParamElementInfo( PT_ZONE_SOURCE, 1, "Advanced Output XML", 1.0f );
	SetParamInfo( PT_COLUMNS, "Columns", FF_TYPE_INTEGER, (float)columns );
	SetParamRange( PT_COLUMNS, 1.0f, 32.0f );
	SetParamInfo( PT_ROWS, "Rows", FF_TYPE_INTEGER, (float)rows );
	SetParamRange( PT_ROWS, 1.0f, 32.0f );
	SetFileParamInfo( PT_XML_FILE, "Slices File", { "xml" }, "" );
	SetOptionParamInfo( PT_GROUP, "Group By", 2, 0.0f );
	SetParamElementInfo( PT_GROUP, 0, "Slices", 0.0f );
	SetParamElementInfo( PT_GROUP, 1, "Screens", 1.0f );
	SetParamInfo( PT_USE, "Use Zones", FF_TYPE_TEXT, "" );
	SetOptionParamInfo( PT_ORDER, "Order", 4, 0.0f );
	SetParamElementInfo( PT_ORDER, 0, "As Listed", 0.0f );
	SetParamElementInfo( PT_ORDER, 1, "Left to Right", 1.0f );
	SetParamElementInfo( PT_ORDER, 2, "Top to Bottom", 2.0f );
	SetParamElementInfo( PT_ORDER, 3, "Shuffled", 3.0f );
	SetParamInfo( PT_SHOW_ZONES, "Show Zones", FF_TYPE_BOOLEAN, false );

	// --- Chase
	SetOptionParamInfo( PT_PATTERN, "Pattern", 9, 0.0f );
	const char* patterns[] = { "Chase >", "Chase <", "Ping-Pong", "Random", "All (Strobe)", "Odd / Even", "Center Out", "Fill", "Audio Meter" };
	for( int i = 0; i < 9; ++i )
		SetParamElementInfo( PT_PATTERN, i, patterns[ i ], (float)i );
	SetParamInfo( PT_WIDTH, "Width", FF_TYPE_INTEGER, 1.0f );
	SetParamRange( PT_WIDTH, 1.0f, 16.0f );
	SetOptionParamInfo( PT_TRIGGER, "Trigger", 4, 0.0f );
	SetParamElementInfo( PT_TRIGGER, 0, "Time (Hz)", 0.0f );
	SetParamElementInfo( PT_TRIGGER, 1, "BPM Sync", 1.0f );
	SetParamElementInfo( PT_TRIGGER, 2, "Audio", 2.0f );
	SetParamElementInfo( PT_TRIGGER, 3, "Manual (Step)", 3.0f );
	SetParamInfo( PT_RATE, "Rate Hz", FF_TYPE_STANDARD, 0.0f );
	SetParamRange( PT_RATE, 0.1f, 30.0f );
	// The SDK clamps STANDARD defaults to 0..1 before the range is known; set the real default afterwards.
	FindParamInfo( PT_RATE )->defaultFloatVal = settings.rateHz;
	SetOptionParamInfo( PT_DIVISION, "Beat Division", kNumDivisions, (float)division );
	for( int i = 0; i < kNumDivisions; ++i )
		SetParamElementInfo( PT_DIVISION, i, kDivisions[ i ].name, (float)i );
	SetParamInfof( PT_STEP, "Step", FF_TYPE_EVENT );
	SetParamInfof( PT_RESET, "Restart", FF_TYPE_EVENT );
	SetOptionParamInfo( PT_BAND, "Audio Band", 4, (float)band );
	SetParamElementInfo( PT_BAND, 0, "Full", 0.0f );
	SetParamElementInfo( PT_BAND, 1, "Bass (kick)", 1.0f );
	SetParamElementInfo( PT_BAND, 2, "Mid", 2.0f );
	SetParamElementInfo( PT_BAND, 3, "High (hats)", 3.0f );
	SetParamInfo( PT_SENS, "Sensitivity", FF_TYPE_STANDARD, settings.sensitivity );
	SetBufferParamInfo( PT_AUDIO, "Audio", kFftSize, FF_USAGE_FFT );

	// --- Strobe
	SetParamInfo( PT_DUTY, "Flash Length", FF_TYPE_STANDARD, settings.duty );
	SetParamInfo( PT_DECAY, "Decay", FF_TYPE_STANDARD, settings.decay );
	SetParamInfo( PT_INNER, "Inner Strobe Hz", FF_TYPE_STANDARD, 0.0f );
	SetParamRange( PT_INNER, 0.0f, 30.0f );
	SetOptionParamInfo( PT_ON_LOOK, "Flash Look", 5, 0.0f );
	SetParamElementInfo( PT_ON_LOOK, 0, "Content", 0.0f );
	SetParamElementInfo( PT_ON_LOOK, 1, "Content Boost", 1.0f );
	SetParamElementInfo( PT_ON_LOOK, 2, "White", 2.0f );
	SetParamElementInfo( PT_ON_LOOK, 3, "Color", 3.0f );
	SetParamElementInfo( PT_ON_LOOK, 4, "Invert", 4.0f );
	SetParamInfo( PT_COLOR_H, "Flash Color", FF_TYPE_HUE, color[ 0 ] );
	SetParamInfo( PT_COLOR_S, "Flash Color_saturation", FF_TYPE_SATURATION, color[ 1 ] );
	SetParamInfo( PT_COLOR_B, "Flash Color_brightness", FF_TYPE_BRIGHTNESS, color[ 2 ] );
	SetParamInfo( PT_COLOR_A, "Flash Color_alpha", FF_TYPE_ALPHA, color[ 3 ] );
	SetParamInfo( PT_OFF_LEVEL, "Off Level", FF_TYPE_STANDARD, offLevel );
	SetOptionParamInfo( PT_OUTSIDE, "Outside Zones", 3, 0.0f );
	SetParamElementInfo( PT_OUTSIDE, 0, "Unchanged", 0.0f );
	SetParamElementInfo( PT_OUTSIDE, 1, "Hidden", 1.0f );
	SetParamElementInfo( PT_OUTSIDE, 2, "Like Off Zones", 2.0f );

	for( FFUInt32 id = PT_ZONE_SOURCE; id <= PT_SHOW_ZONES; ++id )
		SetParamGroup( id, "Zones" );
	for( FFUInt32 id = PT_DUTY; id <= PT_OUTSIDE; ++id )
		SetParamGroup( id, "Strobe" );

	UpdateVisibility( false );
}

FFResult FFGLChaser::InitGL( const FFGLViewportStruct* vp )
{
#if defined( __linux__ )
	glewInit();// Linux builds are only used for offline testing
#endif
	if( !shader.Compile( vertexShaderCode, fragmentShaderCode ) || !quad.Initialise() )
	{
		DeInitGL();
		return FF_FAIL;
	}
	return CFFGLPlugin::InitGL( vp );
}

FFResult FFGLChaser::DeInitGL()
{
	shader.FreeGLResources();
	quad.Release();
	if( zoneTex )
		glDeleteTextures( 1, &zoneTex );
	zoneTex  = 0;
	mapDirty = true;
	return FF_SUCCESS;
}

void FFGLChaser::UpdateVisibility( bool raise )
{
	const bool grid  = zoneSource == 0;
	SetParamVisibility( PT_COLUMNS, grid, raise );
	SetParamVisibility( PT_ROWS, grid, raise );
	SetParamVisibility( PT_XML_FILE, !grid, raise );
	SetParamVisibility( PT_GROUP, !grid, raise );

	const bool audioUsed = trigger == (int)Trigger::Audio || pattern == (int)Pattern::AudioMeter;
	SetParamVisibility( PT_RATE, trigger == (int)Trigger::Free, raise );
	SetParamVisibility( PT_DIVISION, trigger == (int)Trigger::Bpm, raise );
	SetParamVisibility( PT_BAND, audioUsed, raise );
	SetParamVisibility( PT_SENS, trigger == (int)Trigger::Audio, raise );
	SetParamVisibility( PT_WIDTH, pattern != (int)Pattern::AllStrobe && pattern != (int)Pattern::AudioMeter && pattern != (int)Pattern::Fill &&
	                                  pattern != (int)Pattern::OddEven,
	                    raise );
	SetParamVisibility( PT_COLOR_H, onLook == 3, raise );
	SetParamVisibility( PT_COLOR_S, onLook == 3, raise );
	SetParamVisibility( PT_COLOR_B, onLook == 3, raise );
	SetParamVisibility( PT_COLOR_A, onLook == 3, raise );
}

int64_t FFGLChaser::FileStamp( const std::string& path ) const
{
	if( path.empty() )
		return 0;
	namespace fs = std::filesystem;
	std::error_code ec;
#if defined( __cpp_char8_t )
	fs::path p( std::u8string( path.begin(), path.end() ) );
#else
	fs::path p = fs::u8path( path );
#endif
	auto t = fs::last_write_time( p, ec );
	return ec ? 0 : (int64_t)t.time_since_epoch().count();
}

void FFGLChaser::RebuildZones()
{
	zonesDirty = false;
	mapDirty   = true;
	if( zoneSource == 0 )
		zoneSet = MakeGrid( columns, rows );
	else if( xmlPath.empty() )
	{
		zoneSet       = ZoneSet();
		zoneSet.error = "choose a Resolume Advanced Output .xml";
	}
	else
	{
		zoneSet  = LoadAdvancedOutput( xmlPath, (GroupBy)groupBy );
		xmlStamp = FileStamp( xmlPath );
	}

	selected   = ParseSelection( useText, zoneSet.zones.size() );
	chaseOrder = OrderZones( zoneSet, selected, (Order)order, 2024u );
	engine.SetZoneCount( (int)chaseOrder.size() );

	std::string status;
	if( !zoneSet.error.empty() )
	{
		status = "Error: " + zoneSet.error;
		FFGLLog::LogToHost( ( "Slice Chaser: " + zoneSet.error ).c_str() );
	}
	else
		status = std::to_string( chaseOrder.size() ) + " of " + std::to_string( zoneSet.zones.size() ) + " zones (" + zoneSet.info + ")";
	if( status != zoneStatus )
	{
		zoneStatus = status;
		SetParamDisplayName( PT_USE, "Use Zones: " + status, true );
	}
}

void FFGLChaser::BuildZoneMap( int width, int height )
{
	mapDirty = false;
	mapW     = width;
	mapH     = height;
	std::vector< uint8_t > ids = RasterizeZoneMap( zoneSet, selected, width, height );

	// RG8: R = zone id, G = zone-number label (255 = white digit pixel, 128 = dark outline)
	std::vector< uint8_t > rg( (size_t)width * height * 2, 0 );
	for( size_t i = 0; i < ids.size(); ++i )
		rg[ i * 2 ] = ids[ i ];

	for( size_t zi = 0; zi < zoneSet.zones.size() && zi < 255; ++zi )
	{
		if( !selected[ zi ] )
			continue;
		const std::string num = std::to_string( zi + 1 );
		const int scale       = std::max( 2, std::min( width, height ) / 60 );
		const int charW = 3 * scale, gap = scale, textW = (int)num.size() * ( charW + gap ) - gap, textH = 5 * scale;
		const int x0 = (int)std::lround( zoneSet.zones[ zi ].cx * width ) - textW / 2;
		const int y0 = (int)std::lround( zoneSet.zones[ zi ].cy * height ) - textH / 2;// top-down
		auto put     = [ & ]( int x, int ytd, uint8_t v ) {
            if( x < 0 || x >= width || ytd < 0 || ytd >= height )
                return;
            uint8_t& g = rg[ ( (size_t)( height - 1 - ytd ) * width + x ) * 2 + 1 ];
            g          = std::max( g, v );
		};
		for( size_t c = 0; c < num.size(); ++c )
		{
			const uint16_t bits = kDigits[ num[ c ] - '0' ];
			for( int py = 0; py < 5; ++py )
				for( int px = 0; px < 3; ++px )
				{
					if( !( bits >> ( 14 - ( py * 3 + px ) ) & 1 ) )
						continue;
					const int bx = x0 + (int)c * ( charW + gap ) + px * scale;
					const int by = y0 + py * scale;
					for( int yy = -1; yy <= scale; ++yy )// dark outline around each block
						for( int xx = -1; xx <= scale; ++xx )
							put( bx + xx, by + yy, ( xx >= 0 && xx < scale && yy >= 0 && yy < scale ) ? 255 : 128 );
				}
		}
	}

	if( !zoneTex )
		glGenTextures( 1, &zoneTex );
	Scoped2DTextureBinding binding( zoneTex );
	glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_RG8, width, height, 0, GL_RG, GL_UNSIGNED_BYTE, rg.data() );
	glPixelStorei( GL_UNPACK_ALIGNMENT, 4 );// FFGL: restore the default
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
}

FFResult FFGLChaser::ProcessOpenGL( ProcessOpenGLStruct* pGL )
{
	if( !pGL || pGL->numInputTextures < 1 || !pGL->inputTextures[ 0 ] )
		return FF_FAIL;
	const FFGLTextureStruct& in = *pGL->inputTextures[ 0 ];

	using clock    = std::chrono::steady_clock;
	const auto now = clock::now();
	double dt      = haveLastFrame ? std::chrono::duration< double >( now - lastFrame ).count() : 0.0;
	lastFrame      = now;
	haveLastFrame  = true;

	// Advanced Output files are rewritten by Arena when you edit the slices: pick up changes.
	if( zoneSource == 1 && !xmlPath.empty() && now - lastFileCheck > std::chrono::seconds( 1 ) )
	{
		lastFileCheck = now;
		const int64_t st = FileStamp( xmlPath );
		if( st != 0 && st != xmlStamp )
			zonesDirty = true;
	}
	if( zonesDirty )
		RebuildZones();
	if( mapDirty || mapW != (int)in.Width || mapH != (int)in.Height )
		BuildZoneMap( std::max( 1, (int)in.Width ), std::max( 1, (int)in.Height ) );

	// --- audio spectrum from the host
	if( ParamInfo* fft = FindParamInfo( PT_AUDIO ) )
	{
		fftData.resize( fft->elements.size() );
		for( size_t i = 0; i < fftData.size(); ++i )
			fftData[ i ] = fft->elements[ i ].value;
	}

	settings.pattern      = (Pattern)pattern;
	settings.trigger      = (Trigger)trigger;
	settings.band         = (Band)band;
	settings.beatsPerStep = kDivisions[ std::max( 0, std::min( kNumDivisions - 1, division ) ) ].beats;

	FrameInput fi;
	fi.dt          = dt;
	fi.bpm         = bpm;
	fi.barPhase    = barPhase;
	fi.fft         = fftData.empty() ? nullptr : fftData.data();
	fi.fftSize     = fftData.size();
	fi.sampleRate  = sampleRateHz;
	fi.manualSteps = manualSteps;
	fi.reset       = resetPressed;
	manualSteps    = 0;
	resetPressed   = false;

	const std::vector< float >& byPos = engine.Update( settings, fi );
	float levels[ 256 ]               = {};
	for( size_t pos = 0; pos < byPos.size() && pos < chaseOrder.size(); ++pos )
		if( chaseOrder[ pos ] < 256 )
			levels[ chaseOrder[ pos ] ] = byPos[ pos ];

	if( engine.Audio().hit )
		hitFlash = 1.0f;
	else
		hitFlash = std::max( 0.0f, hitFlash - (float)dt * 8.0f );
	const bool meter = showZones && ( trigger == (int)Trigger::Audio || pattern == (int)Pattern::AudioMeter );

	float rgb[ 3 ];
	HSVtoRGB( color[ 0 ] >= 1.0f ? 0.0f : color[ 0 ], color[ 1 ], color[ 2 ], rgb[ 0 ], rgb[ 1 ], rgb[ 2 ] );

	ScopedShaderBinding shaderBinding( shader.GetGLID() );
	glActiveTexture( GL_TEXTURE1 );
	glBindTexture( GL_TEXTURE_2D, zoneTex );
	glActiveTexture( GL_TEXTURE0 );
	glBindTexture( GL_TEXTURE_2D, in.Handle );

	glUniform1i( shader.FindUniform( "inputTexture" ), 0 );
	glUniform1i( shader.FindUniform( "zoneMap" ), 1 );
	glUniform2f( shader.FindUniform( "maxUV" ), in.HardwareWidth ? (float)in.Width / in.HardwareWidth : 1.0f,
	             in.HardwareHeight ? (float)in.Height / in.HardwareHeight : 1.0f );
	glUniform4fv( shader.FindUniform( "levels" ), 64, levels );
	glUniform1i( shader.FindUniform( "onLook" ), onLook );
	glUniform4f( shader.FindUniform( "flashColor" ), rgb[ 0 ], rgb[ 1 ], rgb[ 2 ], color[ 3 ] );
	glUniform1f( shader.FindUniform( "offLevel" ), offLevel );
	glUniform1i( shader.FindUniform( "outside" ), outside );
	glUniform1i( shader.FindUniform( "showZones" ), showZones ? 1 : 0 );
	glUniform1f( shader.FindUniform( "meterLevel" ), meter ? engine.Audio().level : -1.0f );
	glUniform1f( shader.FindUniform( "meterHit" ), hitFlash );
	glUniform2f( shader.FindUniform( "resolution" ), (float)currentViewport.width, (float)currentViewport.height );

	quad.Draw();

	glActiveTexture( GL_TEXTURE1 );
	glBindTexture( GL_TEXTURE_2D, 0 );
	glActiveTexture( GL_TEXTURE0 );
	glBindTexture( GL_TEXTURE_2D, 0 );
	return FF_SUCCESS;
}

FFResult FFGLChaser::SetFloatParameter( unsigned int index, float value )
{
	const int opt = (int)std::lround( value );
	const bool on = value > 0.5f;
	switch( index )
	{
	case PT_ZONE_SOURCE:
		zoneSource = opt;
		zonesDirty = true;
		UpdateVisibility( true );
		break;
	case PT_COLUMNS:
		columns    = std::max( 1, opt );
		zonesDirty = true;
		break;
	case PT_ROWS:
		rows       = std::max( 1, opt );
		zonesDirty = true;
		break;
	case PT_GROUP:
		groupBy    = opt;
		zonesDirty = true;
		break;
	case PT_ORDER:
		order      = opt;
		zonesDirty = true;
		break;
	case PT_SHOW_ZONES: showZones = on; break;
	case PT_PATTERN:
		pattern = opt;
		UpdateVisibility( true );
		break;
	case PT_WIDTH: settings.width = std::max( 1, opt ); break;
	case PT_TRIGGER:
		trigger = opt;
		UpdateVisibility( true );
		break;
	case PT_RATE: settings.rateHz = std::max( 0.01f, value ); break;
	case PT_DIVISION: division = opt; break;
	case PT_STEP:
		if( on )
			++manualSteps;
		break;
	case PT_RESET:
		if( on )
			resetPressed = true;
		break;
	case PT_BAND: band = opt; break;
	case PT_SENS: settings.sensitivity = value; break;
	case PT_DUTY: settings.duty = value; break;
	case PT_DECAY: settings.decay = value; break;
	case PT_INNER: settings.innerStrobeHz = value; break;
	case PT_ON_LOOK:
		onLook = opt;
		UpdateVisibility( true );
		break;
	case PT_COLOR_H: color[ 0 ] = value; break;
	case PT_COLOR_S: color[ 1 ] = value; break;
	case PT_COLOR_B: color[ 2 ] = value; break;
	case PT_COLOR_A: color[ 3 ] = value; break;
	case PT_OFF_LEVEL: offLevel = value; break;
	case PT_OUTSIDE: outside = opt; break;
	case PT_AUDIO: break;// the spectrum arrives as element values, read in ProcessOpenGL
	default: return FF_FAIL;
	}
	return FF_SUCCESS;
}

float FFGLChaser::GetFloatParameter( unsigned int index )
{
	switch( index )
	{
	case PT_ZONE_SOURCE: return (float)zoneSource;
	case PT_COLUMNS: return (float)columns;
	case PT_ROWS: return (float)rows;
	case PT_GROUP: return (float)groupBy;
	case PT_ORDER: return (float)order;
	case PT_SHOW_ZONES: return showZones ? 1.0f : 0.0f;
	case PT_PATTERN: return (float)pattern;
	case PT_WIDTH: return (float)settings.width;
	case PT_TRIGGER: return (float)trigger;
	case PT_RATE: return settings.rateHz;
	case PT_DIVISION: return (float)division;
	case PT_BAND: return (float)band;
	case PT_SENS: return settings.sensitivity;
	case PT_DUTY: return settings.duty;
	case PT_DECAY: return settings.decay;
	case PT_INNER: return settings.innerStrobeHz;
	case PT_ON_LOOK: return (float)onLook;
	case PT_COLOR_H: return color[ 0 ];
	case PT_COLOR_S: return color[ 1 ];
	case PT_COLOR_B: return color[ 2 ];
	case PT_COLOR_A: return color[ 3 ];
	case PT_OFF_LEVEL: return offLevel;
	case PT_OUTSIDE: return (float)outside;
	default: return 0.0f;
	}
}

FFResult FFGLChaser::SetTextParameter( unsigned int index, const char* value )
{
	std::string v = value ? value : "";
	switch( index )
	{
	case PT_XML_FILE:
		if( v != xmlPath )
		{
			xmlPath    = v;
			zonesDirty = true;
		}
		break;
	case PT_USE:
		if( v != useText )
		{
			useText    = v;
			zonesDirty = true;
		}
		break;
	default: return FF_FAIL;
	}
	return FF_SUCCESS;
}

char* FFGLChaser::GetTextParameter( unsigned int index )
{
	switch( index )
	{
	case PT_XML_FILE: return const_cast< char* >( xmlPath.c_str() );
	case PT_USE: return const_cast< char* >( useText.c_str() );
	default: return nullptr;
	}
}

char* FFGLChaser::GetParameterDisplay( unsigned int index )
{
	switch( index )
	{
	case PT_RATE: std::snprintf( displayBuf, sizeof( displayBuf ), "%.2f Hz", settings.rateHz ); return displayBuf;
	case PT_INNER:
		if( settings.innerStrobeHz < 0.01f )
			std::snprintf( displayBuf, sizeof( displayBuf ), "Off" );
		else
			std::snprintf( displayBuf, sizeof( displayBuf ), "%.1f Hz", settings.innerStrobeHz );
		return displayBuf;
	case PT_DUTY:
	case PT_DECAY:
	case PT_SENS:
	case PT_OFF_LEVEL:
		std::snprintf( displayBuf, sizeof( displayBuf ), "%.0f%%", GetFloatParameter( index ) * 100.0f );
		return displayBuf;
	default: return CFFGLPlugin::GetParameterDisplay( index );
	}
}
