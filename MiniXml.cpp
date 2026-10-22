#include "MiniXml.h"
#include <cstdlib>
#include <cstring>

namespace chaser
{
const std::string* XmlNode::Attr( const std::string& key ) const
{
	for( const auto& a : attrs )
		if( a.first == key )
			return &a.second;
	return nullptr;
}

double XmlNode::AttrNum( const std::string& key, double def ) const
{
	const std::string* v = Attr( key );
	if( !v || v->empty() )
		return def;
	char* end = nullptr;
	double d  = std::strtod( v->c_str(), &end );
	return end == v->c_str() ? def : d;
}

const XmlNode* XmlNode::Child( const std::string& childName ) const
{
	for( const auto& c : children )
		if( c->name == childName )
			return c.get();
	return nullptr;
}

std::vector< const XmlNode* > XmlNode::ChildrenNamed( const std::string& childName ) const
{
	std::vector< const XmlNode* > out;
	for( const auto& c : children )
		if( c->name == childName )
			out.push_back( c.get() );
	return out;
}

const XmlNode* XmlNode::Find( const std::string& descendantName ) const
{
	for( const auto& c : children )
	{
		if( c->name == descendantName )
			return c.get();
		if( const XmlNode* f = c->Find( descendantName ) )
			return f;
	}
	return nullptr;
}

namespace
{
void AppendUtf8( std::string& out, unsigned long cp )
{
	if( cp < 0x80 )
		out += (char)cp;
	else if( cp < 0x800 )
	{
		out += (char)( 0xC0 | ( cp >> 6 ) );
		out += (char)( 0x80 | ( cp & 0x3F ) );
	}
	else if( cp < 0x10000 )
	{
		out += (char)( 0xE0 | ( cp >> 12 ) );
		out += (char)( 0x80 | ( ( cp >> 6 ) & 0x3F ) );
		out += (char)( 0x80 | ( cp & 0x3F ) );
	}
	else
	{
		out += (char)( 0xF0 | ( cp >> 18 ) );
		out += (char)( 0x80 | ( ( cp >> 12 ) & 0x3F ) );
		out += (char)( 0x80 | ( ( cp >> 6 ) & 0x3F ) );
		out += (char)( 0x80 | ( cp & 0x3F ) );
	}
}

std::string Decode( const std::string& s )
{
	std::string out;
	for( size_t i = 0; i < s.size(); ++i )
	{
		if( s[ i ] != '&' )
		{
			out += s[ i ];
			continue;
		}
		size_t semi = s.find( ';', i );
		if( semi == std::string::npos || semi - i > 10 )
		{
			out += s[ i ];
			continue;
		}
		std::string ent = s.substr( i + 1, semi - i - 1 );
		if( ent == "amp" )
			out += '&';
		else if( ent == "lt" )
			out += '<';
		else if( ent == "gt" )
			out += '>';
		else if( ent == "quot" )
			out += '"';
		else if( ent == "apos" )
			out += '\'';
		else if( !ent.empty() && ent[ 0 ] == '#' )
		{
			unsigned long cp = ent.size() > 1 && ( ent[ 1 ] == 'x' || ent[ 1 ] == 'X' ) ? std::strtoul( ent.c_str() + 2, nullptr, 16 )
			                                                                            : std::strtoul( ent.c_str() + 1, nullptr, 10 );
			AppendUtf8( out, cp );
		}
		else
		{
			out += s.substr( i, semi - i + 1 );
		}
		i = semi;
	}
	return out;
}

bool IsSpace( char c )
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}
bool IsNameChar( char c )
{
	return !IsSpace( c ) && c != '/' && c != '>' && c != '=' && c != '<' && c != '"' && c != '\'';
}
}// namespace

std::unique_ptr< XmlNode > ParseXml( const std::string& text, std::string& error )
{
	auto doc = std::make_unique< XmlNode >();
	doc->name = "#document";
	std::vector< XmlNode* > stack{ doc.get() };
	size_t i       = 0;
	const size_t n = text.size();

	// Skip UTF-8 BOM
	if( n >= 3 && (unsigned char)text[ 0 ] == 0xEF && (unsigned char)text[ 1 ] == 0xBB && (unsigned char)text[ 2 ] == 0xBF )
		i = 3;

	while( i < n )
	{
		size_t lt = text.find( '<', i );
		if( lt == std::string::npos )
			break;
		i = lt;
		if( text.compare( i, 4, "<!--" ) == 0 )
		{
			size_t e = text.find( "-->", i + 4 );
			i        = e == std::string::npos ? n : e + 3;
			continue;
		}
		if( text.compare( i, 9, "<![CDATA[" ) == 0 )
		{
			size_t e = text.find( "]]>", i + 9 );
			i        = e == std::string::npos ? n : e + 3;
			continue;
		}
		if( text.compare( i, 2, "<?" ) == 0 || text.compare( i, 2, "<!" ) == 0 )
		{
			size_t e = text.find( '>', i );
			i        = e == std::string::npos ? n : e + 1;
			continue;
		}
		if( text.compare( i, 2, "</" ) == 0 )
		{
			size_t e = text.find( '>', i );
			if( e == std::string::npos )
			{
				error = "unterminated closing tag";
				return nullptr;
			}
			std::string name = text.substr( i + 2, e - i - 2 );
			while( !name.empty() && IsSpace( name.back() ) )
				name.pop_back();
			// tolerate mismatches: pop up to the matching element if it exists
			for( size_t k = stack.size(); k-- > 1; )
			{
				if( stack[ k ]->name == name )
				{
					stack.resize( k );
					break;
				}
			}
			i = e + 1;
			continue;
		}

		// Opening tag
		++i;
		size_t ns = i;
		while( i < n && IsNameChar( text[ i ] ) )
			++i;
		auto node  = std::make_unique< XmlNode >();
		node->name = text.substr( ns, i - ns );
		bool selfClosing = false;
		for( ;; )
		{
			while( i < n && IsSpace( text[ i ] ) )
				++i;
			if( i >= n )
			{
				error = "unexpected end of file in <" + node->name + ">";
				return nullptr;
			}
			if( text[ i ] == '>' )
			{
				++i;
				break;
			}
			if( text[ i ] == '/' && i + 1 < n && text[ i + 1 ] == '>' )
			{
				selfClosing = true;
				i += 2;
				break;
			}
			size_t as = i;
			while( i < n && IsNameChar( text[ i ] ) )
				++i;
			std::string key = text.substr( as, i - as );
			while( i < n && IsSpace( text[ i ] ) )
				++i;
			std::string value;
			if( i < n && text[ i ] == '=' )
			{
				++i;
				while( i < n && IsSpace( text[ i ] ) )
					++i;
				if( i < n && ( text[ i ] == '"' || text[ i ] == '\'' ) )
				{
					char q   = text[ i ];
					size_t e = text.find( q, i + 1 );
					if( e == std::string::npos )
					{
						error = "unterminated attribute value";
						return nullptr;
					}
					value = Decode( text.substr( i + 1, e - i - 1 ) );
					i     = e + 1;
				}
				else
				{
					size_t vs = i;
					while( i < n && IsNameChar( text[ i ] ) )
						++i;
					value = text.substr( vs, i - vs );
				}
			}
			if( key.empty() )
			{
				++i;// garbage character, skip it
				continue;
			}
			node->attrs.emplace_back( key, value );
		}
		XmlNode* raw = node.get();
		stack.back()->children.push_back( std::move( node ) );
		if( !selfClosing )
			stack.push_back( raw );
	}

	if( doc->children.empty() )
	{
		error = "no XML elements found";
		return nullptr;
	}
	// Return the first real element as root.
	std::unique_ptr< XmlNode > root = std::move( doc->children.front() );
	return root;
}

}// namespace chaser
