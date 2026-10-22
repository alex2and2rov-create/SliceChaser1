#pragma once
// Zones = the areas of the picture that the chaser switches on and off.
// They come either from a simple grid or from a Resolume Advanced Output
// preset (slices, optionally grouped per screen).

#include <cstdint>
#include <string>
#include <vector>

namespace chaser
{
struct Pt
{
	double x, y;// normalised 0..1, origin top-left (like Resolume's composition)
};

struct Zone
{
	std::string name;
	std::vector< std::vector< Pt > > polys;
	double cx = 0.5, cy = 0.5;// centroid of the bounding box
};

struct ZoneSet
{
	std::vector< Zone > zones;
	int compWidth = 0, compHeight = 0;// from the XML, 0 for grids
	std::string error;               // non-empty when loading failed
	std::string info;                // e.g. "3 screens, 8 slices"
};

enum class GroupBy
{
	Slices,
	Screens
};

enum class Order
{
	AsListed,
	LeftToRight,
	TopToBottom,
	Random
};

ZoneSet MakeGrid( int columns, int rows );

// Parses a Resolume Advanced Output XML (AdvancedOutput.xml or a preset).
ZoneSet ParseAdvancedOutput( const std::string& xmlText, GroupBy group );
ZoneSet LoadAdvancedOutput( const std::string& utf8Path, GroupBy group );

// "1-4, 7, 9-" style selection of 1-based zone numbers. Empty = all.
std::vector< bool > ParseSelection( const std::string& text, size_t count );

// Chase order: indices into zones (only selected ones).
std::vector< int > OrderZones( const ZoneSet& set, const std::vector< bool >& selected, Order order, uint32_t seed );

// Zone-ID map for the GPU: one byte per pixel, 0 = no zone, k = zones[k-1].
// Rows are bottom-up (OpenGL). Only zones with selected[i] are drawn.
// Overlapping zones: the later one wins (same as Resolume's slice stacking).
std::vector< uint8_t > RasterizeZoneMap( const ZoneSet& set, const std::vector< bool >& selected, int width, int height );

}// namespace chaser
