#pragma once

#include <string>

namespace zeronote::pdf {

bool ExportTextToPdf(const std::string& utf8Text, const std::string& path,
                     std::string& error);

}