#include "log.hpp"

#include <iostream>
#include <memory>
#include <streambuf>

namespace {

//stream buffer wrapper/handler
class LogStreamBuf : public std::streambuf {
public:
    LogStreamBuf() = delete;
    explicit LogStreamBuf(NewStarInjectedLoggerFunctionType injectedLoggerFunction);

protected:
    int overflow(int ch) override;

    int sync() override;
    
    NewStarInjectedLoggerFunctionType mInjectedLoggerFunction;
    std::string mLineBuffer;
};

LogStreamBuf::LogStreamBuf(NewStarInjectedLoggerFunctionType injectedLoggerFunction) : mInjectedLoggerFunction(std::move(injectedLoggerFunction)){}

int LogStreamBuf::overflow(int ch) {
    if (ch != traits_type::eof()) {
        if(ch == '\n') { //flush and forward to logger function on newline
            mInjectedLoggerFunction(mLineBuffer);
            mLineBuffer.clear();
        }
        else {
            mLineBuffer.push_back(static_cast<char>(ch));
        }
    }
    return ch;
}

int LogStreamBuf::sync() {
    if(!mLineBuffer.empty()) {
        mInjectedLoggerFunction(mLineBuffer);
        mLineBuffer.clear();
    }
    return 0;
} // namespace

//state "singleton"
class LogState {
public:
    explicit LogState(NewStarInjectedLoggerFunctionType loggerFunction, NewStarInjectedLoggerFunctionType loggerErrorFunction);

    std::ostream& log() { return mLogStream; }
    std::ostream& err() { return mErrStream; }

private:
    LogStreamBuf mBufLog;
    LogStreamBuf mBufErr;
    std::ostream mLogStream;
    std::ostream mErrStream;
};

std::unique_ptr<LogState> logState;

LogState::LogState(NewStarInjectedLoggerFunctionType loggerFunction, NewStarInjectedLoggerFunctionType loggerErrorFunction)
        : mBufLog(std::move(loggerFunction)),
        mBufErr(std::move(loggerErrorFunction))
        , mLogStream(&mBufLog)
        , mErrStream(&mBufErr)
    {}
}

namespace newstar {

std::ostream& log()
{
    if(!logState) return std::cout;
    return logState->log();
}

std::ostream& err()
{
    if(!logState) return std::cerr;
    return logState->err();
}

void initializeLogState(NewStarInjectedLoggerFunctionType loggerFunction, NewStarInjectedLoggerFunctionType loggerErrorFunction) {
    logState = std::make_unique<LogState>(std::move(loggerFunction), std::move(loggerErrorFunction));
}

void shutdownLogState() {
    logState.reset();
}

}