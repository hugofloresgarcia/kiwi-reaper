#pragma once

#include "spdlog/spdlog.h"
#include "spdlog/sinks/basic_file_sink.h"

#include <iostream>


using spdlog::info;
using spdlog::debug;
using spdlog::warn;

#define LOG_LEVEL spdlog::level::debug
#define LOG_FLUSH_INTERVAL std::chrono::seconds(1)

inline void kiwi_logger_init(const std::string& path) {
    try 
    {
        auto logger = spdlog::basic_logger_mt("log", path);
        logger->set_level(LOG_LEVEL);
        spdlog::set_default_logger(logger);
        spdlog::set_pattern("[%H:%M:%S] [%L] [%@] [thread %t] %v");
        spdlog::flush_every(LOG_FLUSH_INTERVAL);

    }
    catch (const spdlog::spdlog_ex &ex)
    {
        std::cout << "Log init failed: " << ex.what() << std::endl;
    };
}


