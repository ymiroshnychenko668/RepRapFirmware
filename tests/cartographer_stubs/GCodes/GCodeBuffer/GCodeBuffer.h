#pragma once
#include <RepRapFirmware.h>
#include <map>
class GCodeBuffer {
    std::map<char,std::string> parameters;
    char current = 0;
public:
    GCodeBuffer(std::initializer_list<std::pair<const char,std::string>> init) : parameters(init) { }
    bool Seen(char key) { current = key; return parameters.count(key) != 0; }
    void GetQuotedString(const StringRef& result) { result.copy(parameters.at(current).c_str()); }
    uint32_t GetUIValue() { return std::stoul(parameters.at(current)); }
    float GetFValue() { return std::stof(parameters.at(current)); }
};
