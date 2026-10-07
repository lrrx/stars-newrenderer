#pragma once

#include <streambuf>
#include <functional>
#include <memory>
#include <iostream>

using InjectedLoggerFunctionType = std::function<void(std::string const&)>;
namespace newstar {

std::ostream& log();
std::ostream& err();

void initializeLogState(InjectedLoggerFunctionType loggerFunction, InjectedLoggerFunctionType loggerErrorFunction);
void shutdownLogState();

}