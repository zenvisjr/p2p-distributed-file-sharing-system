#pragma once
#include <cstdlib>
#include <cstring>
#include <string>

inline std::string envStr(const char *k, const char *def) {
  const char *v = std::getenv(k);
  return (v && *v) ? std::string(v) : std::string(def);
}
inline int envInt(const char *k, int def) {
  const char *v = std::getenv(k);
  return (v && *v) ? std::atoi(v) : def;
}
inline float envFloat(const char *k, float def) {
  const char *v = std::getenv(k);
  return (v && *v) ? std::atof(v) : def;
}

