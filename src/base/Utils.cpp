#include "Utils.h"

namespace tr {

    std::string rmmc(const std::string& s) { return s.find("minecraft:") == 0 ? s.substr(9) : s; }
}  // namespace tr