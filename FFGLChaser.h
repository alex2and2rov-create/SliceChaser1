#pragma once
#include <FFGLSDK.h>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>
#include "ChaseEngine.h"
#include "Zones.h"

class FFGLChaser : public CFFGLPlugin
{
public:
	FFGLChaser();

	FFResult InitGL( const FFGLViewportStruct* vp ) override;
	FFResult ProcessOpenGL( ProcessOpenGLStruct* pGL ) override;
	FFResult DeInitGL() override;

	FFResult SetFloatParameter( unsigned int index, float value ) override;
	float GetFloatParameter( unsigned int index ) override;
	FFResult SetTextParameter( unsigned int index, const char* value ) override;
	char* GetTextParameter( unsigned int index ) override;
	char* GetParameterDisplay( unsigned int index ) override;

private:
	void RebuildZones();
	void BuildZoneMap( int width, int height );
	void UpdateVisibility( bool raise );
	int64_t FileStamp( const std::string& path ) const;

	// --- zones
	int zoneSource    = 0;// 0 grid, 1 xml
	int columns       = 4, rows = 1;
	std::string xmlPath;
	int groupBy       = 0;
	std::string useText;
	int order         = 0;
	bool showZones    = false;

	// --- chase
	chaser::EngineSettings settings;
	int pattern       = 0;
	int trigger       = 0;
	int division      = 4;// index into kDivisions
	int band          = 1;
	int manualSteps   = 0;
	bool resetPressed = false;

	// --- look
	int onLook        = 0;
	float color[ 4 ]  = { 0.0f, 0.0f, 1.0f, 1.0f };// H S B A
	float offLevel    = 0.0f;
	int outside       = 0;

	// --- state
	chaser::ZoneSet zoneSet;
	std::vector< bool > selected;
	std::vector< int > chaseOrder;// position -> zone index
	chaser::ChaseEngine engine;
	bool zonesDirty  = true;
	bool mapDirty    = true;
	int mapW = 0, mapH = 0;
	int64_t xmlStamp = 0;
	std::chrono::steady_clock::time_point lastFrame, lastFileCheck;
	bool haveLastFrame = false;
	float hitFlash     = 0.0f;
	std::vector< float > fftData;
	unsigned sampleRateHz = 44100;
	std::string zoneStatus;

	GLuint zoneTex = 0;
	ffglex::FFGLShader shader;
	ffglex::FFGLScreenQuad quad;
	char displayBuf[ 128 ] = {};

public:
	void SetSampleRate( unsigned int rate ) override
	{
		CFFGLPlugin::SetSampleRate( rate );
		sampleRateHz = rate;
	}
};
