#include "Zones.h"
#include "MiniXml.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <numeric>
#include <random>
#include <sstream>

namespace fs = std::filesystem;

namespace chaser
{
namespace
{
void UpdateCentroid( Zone& z )
{
	double x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
	for( const auto& p : z.polys )
		for( const auto& v : p )
		{
			x0 = std::min( x0, v.x );
			y0 = std::min( y0, v.y );
			x1 = std::max( x1, v.x );
			y1 = std::max( y1, v.y );
		}
	if( x1 >= x0 )
	{
		z.cx = ( x0 + x1 ) * 0.5;
		z.cy = ( y0 + y1 ) * 0.5;
	}
}

std::string ParamValue( const XmlNode* paramsParent, const char* paramsName, const char* paramName )
{
	if( !paramsParent )
		return {};
	for( const XmlNode* params : paramsParent->ChildrenNamed( "Params" ) )
	{
		const std::string* pn = params->Attr( "name" );
		if( paramsName && ( !pn || *pn != paramsName ) )
			continue;
		for( const auto& c : params->children )
		{
			const std::string* n = c->Attr( "name" );
			if( n && *n == paramName )
			{
				const std::string* v = c->Attr( "value" );
				return v ? *v : std::string();
			}
		}
	}
	return {};
}

bool StartsWith( const std::string& s, const char* prefix )
{
	return s.rfind( prefix, 0 ) == 0;
}

bool Contains( const std::string& s, const char* part )
{
	return s.find( part ) != std::string::npos;
}

// The input-side outline of a slice: the first element named Input* that holds <v x y/> points.
const XmlNode* FindInputShape( const XmlNode* layer )
{
	for( const auto& c : layer->children )
	{
		if( !StartsWith( c->name, "Input" ) )
			continue;
		if( c->ChildrenNamed( "v" ).size() >= 3 )
			return c.get();
		// polygon-type layers may nest their points one level deeper
		for( const auto& cc : c->children )
			if( cc->ChildrenNamed( "v" ).size() >= 3 )
				return cc.get();
	}
	return nullptr;
}
}// namespace

ZoneSet MakeGrid( int columns, int rows )
{
	ZoneSet set;
	columns = std::max( 1, std::min( 64, columns ) );
	rows    = std::max( 1, std::min( 64, rows ) );
	for( int r = 0; r < rows; ++r )
		for( int c = 0; c < columns; ++c )
		{
			Zone z;
			const double x0 = (double)c / columns, x1 = (double)( c + 1 ) / columns;
			const double y0 = (double)r / rows, y1 = (double)( r + 1 ) / rows;
			z.polys.push_back( { { x0, y0 }, { x1, y0 }, { x1, y1 }, { x0, y1 } } );
			z.name = "Cell " + std::to_string( r * columns + c + 1 );
			UpdateCentroid( z );
			set.zones.push_back( std::move( z ) );
		}
	set.info = std::to_string( columns ) + " x " + std::to_string( rows ) + " grid";
	return set;
}

ZoneSet ParseAdvancedOutput( const std::string& xmlText, GroupBy group )
{
	ZoneSet set;
	std::string err;
	auto root = ParseXml( xmlText, err );
	if( !root )
	{
		set.error = "XML error: " + err;
		return set;
	}
	const XmlNode* setup = root->name == "ScreenSetup" ? root.get() : root->Find( "ScreenSetup" );
	if( !setup )
	{
		set.error = "not a Resolume Advanced Output file (no ScreenSetup)";
		return set;
	}
	if( const XmlNode* size = setup->Child( "CurrentCompositionTextureSize" ) )
	{
		set.compWidth  = (int)size->AttrNum( "width" );
		set.compHeight = (int)size->AttrNum( "height" );
	}

	struct RawSlice
	{
		std::string screen, name;
		std::vector< Pt > pts;// composition pixels
	};
	std::vector< RawSlice > slices;
	int screenCount = 0;

	const XmlNode* screens = setup->Child( "screens" );
	if( screens )
	{
		for( const auto& screen : screens->children )
		{
			// Skip disabled screens
			if( ParamValue( screen.get(), "Params", "Enabled" ) == "0" )
				continue;
			std::string screenName = ParamValue( screen.get(), "Params", "Name" );
			if( screenName.empty() )
				if( const std::string* n = screen->Attr( "name" ) )
					screenName = *n;
			const XmlNode* layers = screen->Child( "layers" );
			if( !layers )
				continue;
			bool any = false;
			for( const auto& layer : layers->children )
			{
				// Masks and crops cut away from other slices; they are not areas of their own.
				if( Contains( layer->name, "Mask" ) || Contains( layer->name, "Crop" ) )
					continue;
				if( ParamValue( layer.get(), "Common", "Enabled" ) == "0" )
					continue;
				const XmlNode* shape = FindInputShape( layer.get() );
				if( !shape )
					continue;
				RawSlice s;
				s.screen = screenName.empty() ? "Screen " + std::to_string( screenCount + 1 ) : screenName;
				s.name   = ParamValue( layer.get(), "Common", "Name" );
				if( s.name.empty() )
					s.name = "Slice " + std::to_string( slices.size() + 1 );
				for( const XmlNode* v : shape->ChildrenNamed( "v" ) )
					s.pts.push_back( { v->AttrNum( "x" ), v->AttrNum( "y" ) } );
				slices.push_back( std::move( s ) );
				any = true;
			}
			if( any )
				++screenCount;
		}
	}

	if( slices.empty() )
	{
		set.error = "no slices found in the file";
		return set;
	}

	// Normalise by the composition size (or by the slices' extent if it's missing).
	double W = set.compWidth, H = set.compHeight;
	if( W <= 0 || H <= 0 )
	{
		W = H = 1;
		for( const auto& s : slices )
			for( const auto& p : s.pts )
			{
				W = std::max( W, p.x );
				H = std::max( H, p.y );
			}
	}
	for( auto& s : slices )
		for( auto& p : s.pts )
		{
			p.x /= W;
			p.y /= H;
		}

	if( group == GroupBy::Screens )
	{
		std::map< std::string, size_t > index;
		for( auto& s : slices )
		{
			auto it = index.find( s.screen );
			if( it == index.end() )
			{
				index[ s.screen ] = set.zones.size();
				Zone z;
				z.name = s.screen;
				set.zones.push_back( std::move( z ) );
				it = index.find( s.screen );
			}
			set.zones[ it->second ].polys.push_back( s.pts );
		}
	}
	else
	{
		for( auto& s : slices )
		{
			Zone z;
			z.name = s.screen + " / " + s.name;
			z.polys.push_back( s.pts );
			set.zones.push_back( std::move( z ) );
		}
	}
	for( auto& z : set.zones )
		UpdateCentroid( z );
	if( set.zones.size() > 255 )
		set.zones.resize( 255 );

	set.info = std::to_string( screenCount ) + " screens, " + std::to_string( slices.size() ) + " slices";
	return set;
}

ZoneSet LoadAdvancedOutput( const std::string& utf8Path, GroupBy group )
{
	ZoneSet set;
#if defined( __cpp_char8_t )
	fs::path p( std::u8string( utf8Path.begin(), utf8Path.end() ) );
#else
	fs::path p = fs::u8path( utf8Path );
#endif
	std::ifstream in( p, std::ios::binary );
	if( !in )
	{
		set.error = "cannot open " + utf8Path;
		return set;
	}
	std::stringstream ss;
	ss << in.rdbuf();
	return ParseAdvancedOutput( ss.str(), group );
}

std::vector< bool > ParseSelection( const std::string& text, size_t count )
{
	std::vector< bool > sel( count, false );
	bool any = false;
	std::string t;
	for( char c : text )
		t += ( c == ';' || c == ' ' ) ? ',' : c;
	std::stringstream ss( t );
	std::string item;
	while( std::getline( ss, item, ',' ) )
	{
		if( item.empty() )
			continue;
		size_t dash = item.find( '-' );
		long a, b;
		if( dash == std::string::npos )
			a = b = std::strtol( item.c_str(), nullptr, 10 );
		else
		{
			std::string l = item.substr( 0, dash ), r = item.substr( dash + 1 );
			a             = l.empty() ? 1 : std::strtol( l.c_str(), nullptr, 10 );
			b             = r.empty() ? (long)count : std::strtol( r.c_str(), nullptr, 10 );
		}
		if( a > b )
			std::swap( a, b );
		for( long k = std::max( 1L, a ); k <= std::min( (long)count, b ); ++k )
		{
			sel[ k - 1 ] = true;
			any          = true;
		}
	}
	if( !any )
		std::fill( sel.begin(), sel.end(), true );
	return sel;
}

std::vector< int > OrderZones( const ZoneSet& set, const std::vector< bool >& selected, Order order, uint32_t seed )
{
	std::vector< int > idx;
	for( size_t i = 0; i < set.zones.size(); ++i )
		if( i < selected.size() && selected[ i ] )
			idx.push_back( (int)i );
	const auto& z = set.zones;
	const double eps = 1e-3;
	switch( order )
	{
	case Order::LeftToRight:
		std::stable_sort( idx.begin(), idx.end(), [ & ]( int a, int b ) {
			if( std::fabs( z[ a ].cx - z[ b ].cx ) > eps )
				return z[ a ].cx < z[ b ].cx;
			return z[ a ].cy < z[ b ].cy;
		} );
		break;
	case Order::TopToBottom:
		std::stable_sort( idx.begin(), idx.end(), [ & ]( int a, int b ) {
			if( std::fabs( z[ a ].cy - z[ b ].cy ) > eps )
				return z[ a ].cy < z[ b ].cy;
			return z[ a ].cx < z[ b ].cx;
		} );
		break;
	case Order::Random:
	{
		std::mt19937 rng( seed );
		std::shuffle( idx.begin(), idx.end(), rng );
		break;
	}
	default: break;
	}
	return idx;
}

std::vector< uint8_t > RasterizeZoneMap( const ZoneSet& set, const std::vector< bool >& selected, int width, int height )
{
	std::vector< uint8_t > map( (size_t)width * height, 0 );
	std::vector< double > xs;
	for( size_t zi = 0; zi < set.zones.size() && zi < 255; ++zi )
	{
		if( zi >= selected.size() || !selected[ zi ] )
			continue;
		const uint8_t id = (uint8_t)( zi + 1 );
		for( const auto& poly : set.zones[ zi ].polys )
		{
			if( poly.size() < 3 )
				continue;
			double minY = 1e9, maxY = -1e9;
			for( const auto& p : poly )
			{
				minY = std::min( minY, p.y * height );
				maxY = std::max( maxY, p.y * height );
			}
			const int y0 = std::max( 0, (int)std::floor( minY ) );
			const int y1 = std::min( height - 1, (int)std::ceil( maxY ) );
			for( int y = y0; y <= y1; ++y )
			{
				const double sy = y + 0.5;// sample at pixel centres
				xs.clear();
				for( size_t k = 0; k < poly.size(); ++k )
				{
					const Pt& a = poly[ k ];
					const Pt& b = poly[ ( k + 1 ) % poly.size() ];
					const double ay = a.y * height, by = b.y * height;
					if( ( ay <= sy && by > sy ) || ( by <= sy && ay > sy ) )
						xs.push_back( a.x * width + ( sy - ay ) / ( by - ay ) * ( b.x - a.x ) * width );
				}
				std::sort( xs.begin(), xs.end() );
				uint8_t* row = &map[ (size_t)( height - 1 - y ) * width ];// bottom-up rows
				for( size_t k = 0; k + 1 < xs.size(); k += 2 )
				{
					const int xa = std::max( 0, (int)std::ceil( xs[ k ] - 0.5 ) );
					const int xb = std::min( width - 1, (int)std::ceil( xs[ k + 1 ] - 0.5 ) - 1 );
					for( int x = xa; x <= xb; ++x )
						row[ x ] = id;
				}
			}
		}
	}
	return map;
}

}// namespace chaser
