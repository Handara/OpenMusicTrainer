#pragma once

#include <string>
#include <utility>
#include <vector>

// A small XML reader: enough for files other programs write (Guitar Pro's scores), read whole into a tree. Elements,
// attributes, text and CDATA; the five named entities and numeric ones; comments, the declaration and a DOCTYPE are
// skipped. Pure logic.

struct XmlNode {
    std::string name;
    std::vector<std::pair<std::string, std::string>> attributes;
    std::string text;            // the text directly inside it (CDATA included), trimmed
    std::vector<XmlNode> children;

    const XmlNode* child(const std::string& name) const;              // the first child of that name, or null
    std::vector<const XmlNode*> childrenNamed(const std::string& name) const;
    std::string attribute(const std::string& name) const;             // "" when it has none
    std::string childText(const std::string& name) const;             // the named child's text, "" when there's none
};

// The document's root element. False, with where and why, for text that isn't well-formed XML.
bool parseXml(const std::string& text, XmlNode& root, std::string& error);
