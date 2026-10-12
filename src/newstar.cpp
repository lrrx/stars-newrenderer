#include <newstar.hpp>

#include <RawStar.hpp>
#include <vector>

#include <memory>
#include "StarRenderer.hpp"
#include "log/log.hpp"
#include <StaticGlobalConfig.hpp>

std::unique_ptr<newstar::StarRenderer> starRenderer;

//assumes that gl context already exists
void newstarInit(glm::uvec2 const& screenSize, std::string const& shaderBasePath, std::string const& cacheBasePath) {
    newstar::initialize();

    newstar::initStaticGlobalConfig(newstar::StaticGlobalConfig(shaderBasePath, cacheBasePath));

    newstar::initializeLogState(
        [](std::string const& s){std::cout << "[newstar] " << s << std::endl;},
    [](std::string const& s){std::cout << "[newstar] " << s << std::endl;}
    );

    starRenderer = std::make_unique<newstar::StarRenderer>(screenSize);
}

void newstarSetdata(std::vector<RawStar> const& rawStars) {
    starRenderer->preprocessStars(rawStars);
    starRenderer->prepareGpuBuffers();
}

void newstarRender(
    glm::mat4 modelViewMatrix,
    glm::mat4 inverseModelViewMatrix,
    glm::mat4 projectionMatrix,
    glm::mat4 inverseProjectionMatrix,
    float luminanceMultiplicator,
    bool hdrEnabled
) {

    starRenderer->run(
        modelViewMatrix,
        inverseModelViewMatrix,
        projectionMatrix,
        inverseProjectionMatrix,
        luminanceMultiplicator,
        hdrEnabled);
}

StarRendererConfig& newstarConfig() {
    return starRenderer->config();
}

void newstarDeInit() {
    starRenderer.reset(); //trigger destructor through unique_ptr reset
    ::newstar::shutdownLogState();
}