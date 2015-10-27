#pragma once

#include <mutex>
#include <condition_variable>

void serverStart();
extern std::mutex muEvent;
extern std::condition_variable conEvent;
