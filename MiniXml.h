#pragma once
// Tiny tolerant XML reader — just enough for Resolume's Advanced Output files.
// Elements, attributes, self-closing tags, comments, <?...?>, <!...>, CDATA and
// the standard entities. Text content is ignored.

#include <memory>
#include <string>
#include <vector>

namespace chaser
{
struct XmlNode
{
	std::string name;
	std::vector< std::pair< std::string, std::string > > attrs;
	std::vector< std::unique_ptr< XmlNode > > children;

	const std::string* Attr( const std::string& key ) const;
	double AttrNum( const std::string& key, double def = 0.0 ) const;
	const XmlNode* Child( const std::string& childName ) const;
	std::vector< const XmlNode* > ChildrenNamed( const std::string& childName ) const;
	// Depth-first search for the first descendant with this name.
	const XmlNode* Find( const std::string& descendantName ) const;
};

// Returns the document's root element, or nullptr (error message in `error`).
std::unique_ptr< XmlNode > ParseXml( const std::string& text, std::string& error );

}// namespace chaser
