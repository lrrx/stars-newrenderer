#include "StaticGlobalConfig.hpp"
#include <cassert>

namespace newstar{

std::unique_ptr<StaticGlobalConfig> _staticGlobalConfig;

StaticGlobalConfig::StaticGlobalConfig(std::string const& shaderBasePath, std::string const& cacheBasePath) 
: SHADER_BASE_PATH{shaderBasePath},
CACHE_BASE_PATH{cacheBasePath}
{}

void initStaticGlobalConfig(StaticGlobalConfig const& sgConfig) {
    _staticGlobalConfig = std::make_unique<StaticGlobalConfig>(sgConfig);
}

StaticGlobalConfig const& staticGlobalConfig() {
    assert(_staticGlobalConfig != nullptr);
    return *_staticGlobalConfig;
}

}