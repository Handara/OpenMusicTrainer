#include "core/xml.h"

#include <cctype>
#include <cstdlib>

const XmlNode* XmlNode::child(const std::string& childName) const {
    for (const XmlNode& node : children) if (node.name == childName) return &node;
    return nullptr;
}

std::vector<const XmlNode*> XmlNode::childrenNamed(const std::string& childName) const {
    std::vector<const XmlNode*> found;
    for (const XmlNode& node : children) if (node.name == childName) found.push_back(&node);
    return found;
}

std::string XmlNode::attribute(const std::string& attributeName) const {
    for (const auto& [key, value] : attributes) if (key == attributeName) return value;
    return "";
}

std::string XmlNode::childText(const std::string& childName) const {
    const XmlNode* node = child(childName);
    return node ? node->text : "";
}

namespace {

struct Reader {
    const std::string& text;
    size_t at = 0;
    std::string error;

    bool done() const { return at >= text.size(); }
    bool startsWith(const char* prefix) const { return text.compare(at, std::char_traits<char>::length(prefix), prefix) == 0; }
    void skipSpace(){ while (!done() && std::isspace((unsigned char)text[at])) at++; }

    bool fail(const std::string& why){
        if (error.empty()){
            int line = 1;
            for (size_t i = 0; i < at && i < text.size(); i++) if (text[i] == '\n') line++;
            error = "line " + std::to_string(line) + ": " + why;
        }
        return false;
    }

    // Skips to just after `end`
    bool skipPast(const char* end){
        size_t found = text.find(end, at);
        if (found == std::string::npos) return fail(std::string("no closing ") + end);
        at = found + std::char_traits<char>::length(end);
        return true;
    }

    // Comments, processing instructions (the declaration) and a DOCTYPE, wherever they may be
    bool skipMarkup(){
        while (true){
            skipSpace();
            if (startsWith("<!--")){ if (!skipPast("-->")) return false; }
            else if (startsWith("<?")){ if (!skipPast("?>")) return false; }
            else if (startsWith("<!DOCTYPE")){ if (!skipPast(">")) return false; }
            else return true;
        }
    }

    std::string name(){
        size_t start = at;
        while (!done() && (std::isalnum((unsigned char)text[at]) || text[at] == '_' || text[at] == '-' || text[at] == '.' || text[at] == ':')) at++;
        return text.substr(start, at - start);
    }
};

// Text with its entities turned back into characters
std::string decode(const std::string& raw){
    std::string out;
    out.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); i++){
        if (raw[i] != '&'){ out += raw[i]; continue; }
        size_t end = raw.find(';', i);
        if (end == std::string::npos){ out += raw[i]; continue; }
        std::string entity = raw.substr(i + 1, end - i - 1);
        if (entity == "lt") out += '<';
        else if (entity == "gt") out += '>';
        else if (entity == "amp") out += '&';
        else if (entity == "quot") out += '"';
        else if (entity == "apos") out += '\'';
        else if (!entity.empty() && entity[0] == '#'){
            unsigned long code = entity.size() > 1 && (entity[1] == 'x' || entity[1] == 'X') ? std::strtoul(entity.c_str() + 2, nullptr, 16)
                                                                                         : std::strtoul(entity.c_str() + 1, nullptr, 10);
            // As UTF-8
            if (code < 0x80) out += (char)code;
            else if (code < 0x800){ out += (char)(0xC0 | (code >> 6)); out += (char)(0x80 | (code & 0x3F)); }
            else if (code < 0x10000){ out += (char)(0xE0 | (code >> 12)); out += (char)(0x80 | ((code >> 6) & 0x3F)); out += (char)(0x80 | (code & 0x3F)); }
            else { out += (char)(0xF0 | (code >> 18)); out += (char)(0x80 | ((code >> 12) & 0x3F)); out += (char)(0x80 | ((code >> 6) & 0x3F)); out += (char)(0x80 | (code & 0x3F)); }
        } else {
            out += raw.substr(i, end - i + 1); // not one it knows: kept as it is
        }
        i = end;
    }
    return out;
}

std::string trim(const std::string& text){
    size_t start = 0, end = text.size();
    while (start < end && std::isspace((unsigned char)text[start])) start++;
    while (end > start && std::isspace((unsigned char)text[end - 1])) end--;
    return text.substr(start, end - start);
}

bool parseElement(Reader& reader, XmlNode& node, int depth){
    if (depth > 200) return reader.fail("nested too deep");
    if (!reader.startsWith("<")) return reader.fail("expected an element");
    reader.at++;
    node.name = reader.name();
    if (node.name.empty()) return reader.fail("an element without a name");
    // Its attributes
    while (true){
        reader.skipSpace();
        if (reader.done()) return reader.fail("unfinished <" + node.name + ">");
        if (reader.startsWith("/>")){ reader.at += 2; return true; }
        if (reader.startsWith(">")){ reader.at++; break; }
        std::string key = reader.name();
        if (key.empty()) return reader.fail("a strange character in <" + node.name + ">");
        reader.skipSpace();
        if (!reader.startsWith("=")) return reader.fail("attribute " + key + " without a value");
        reader.at++;
        reader.skipSpace();
        if (reader.done() || (reader.text[reader.at] != '"' && reader.text[reader.at] != '\'')) return reader.fail("attribute " + key + " without quotes");
        char quote = reader.text[reader.at++];
        size_t end = reader.text.find(quote, reader.at);
        if (end == std::string::npos) return reader.fail("attribute " + key + " never closed");
        node.attributes.push_back({ key, decode(reader.text.substr(reader.at, end - reader.at)) });
        reader.at = end + 1;
    }
    // Its content, to its closing tag
    std::string text;
    while (true){
        if (reader.done()) return reader.fail("<" + node.name + "> never closed");
        if (reader.startsWith("</")){
            reader.at += 2;
            std::string closing = reader.name();
            if (closing != node.name) return reader.fail("</" + closing + "> closes <" + node.name + ">");
            reader.skipSpace();
            if (!reader.startsWith(">")) return reader.fail("unfinished </" + closing + ">");
            reader.at++;
            node.text = trim(text);
            return true;
        }
        if (reader.startsWith("<![CDATA[")){
            size_t start = reader.at + 9, end = reader.text.find("]]>", start);
            if (end == std::string::npos) return reader.fail("CDATA never closed");
            text += reader.text.substr(start, end - start);
            reader.at = end + 3;
        } else if (reader.startsWith("<!--")){
            if (!reader.skipPast("-->")) return false;
        } else if (reader.startsWith("<?")){
            if (!reader.skipPast("?>")) return false;
        } else if (reader.startsWith("<")){
            node.children.emplace_back();
            if (!parseElement(reader, node.children.back(), depth + 1)) return false;
        } else {
            size_t next = reader.text.find('<', reader.at);
            if (next == std::string::npos) next = reader.text.size();
            text += decode(reader.text.substr(reader.at, next - reader.at));
            reader.at = next;
        }
    }
}

} // namespace

bool parseXml(const std::string& text, XmlNode& root, std::string& error){
    Reader reader{ text, 0, "" };
    if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) reader.at = 3; // a UTF-8 byte order mark
    root = XmlNode{};
    if (!reader.skipMarkup() || !parseElement(reader, root, 0) || !reader.skipMarkup()){
        error = reader.error;
        return false;
    }
    if (!reader.done()){
        reader.fail("more after the root element");
        error = reader.error;
        return false;
    }
    return true;
}
