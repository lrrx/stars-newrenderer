#pragma once

#include <newstar.hpp>
#include <iostream>

namespace newstar {

std::ostream& log();
std::ostream& err();

void initializeLogState(NewStarInjectedLoggerFunctionType loggerFunction, NewStarInjectedLoggerFunctionType loggerErrorFunction);
void shutdownLogState();

}