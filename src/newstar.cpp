#include <newstar.hpp>

#include <RawStar.hpp>
#include <vector>

#include <memory>
#include "StarRenderer.hpp"
#include "log/log.hpp"
#include <StaticGlobalConfig.hpp>

std::unique_ptr<newstar::StarRenderer> starRenderer;

//assumes that gl context already exists
void newstarInit(glm::uvec2 const& viewportSize, std::string const& shaderBasePath, std::string const& cacheBasePath,
NewStarInjectedLoggerFunctionType loggerFunction, NewStarInjectedLoggerFunctionType loggerErrorFunction) {
    newstar::initialize();

    newstar::initStaticGlobalConfig(newstar::StaticGlobalConfig(shaderBasePath, cacheBasePath));

    newstar::initializeLogState(
        loggerFunction,
        loggerErrorFunction
    );

    starRenderer = std::make_unique<newstar::StarRenderer>(viewportSize);
}

void newstarSetdata(std::vector<RawStar> const& rawStars) {
    starRenderer->preprocessStars(rawStars);
    starRenderer->prepareGpuBuffers();
}

void newstarRender(
    glm::mat4 modelViewMatrix,
    glm::mat4 inverseModelViewMatrix,
    glm::mat4 projectionMatrix,
    glm::mat4 inverseProjectionMatrix
) {

    starRenderer->run(
        modelViewMatrix,
        inverseModelViewMatrix,
        projectionMatrix,
        inverseProjectionMatrix);
}

void newstarResizeScreen(
    glm::uvec2 screenSize
) {
    starRenderer->resize(screenSize);
}

StarRendererConfig& newstarConfig() {
    return starRenderer->config();
}

void newstarDeInit() {
    starRenderer.reset(); //trigger destructor through unique_ptr reset
    ::newstar::shutdownLogState();
}