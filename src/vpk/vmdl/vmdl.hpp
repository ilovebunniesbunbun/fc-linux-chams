#pragma once

#include <string>
#include <vector>
#include <optional>
#include "model.hpp"

namespace VmdlParser {

struct Entry {
    std::string Path;
    std::string Name;
};

std::vector<Entry> ListAll(const std::string& Filter = "");

AgentParser::AgentMesh Load(const std::string& Path);

const std::string& LastError();

bool ExportToGLTF(const AgentParser::AgentMesh& mesh, const std::string& outputPath);
bool ExportToGLB(const AgentParser::AgentMesh& mesh, const std::string& outputPath);

}
