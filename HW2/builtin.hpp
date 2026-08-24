#pragma once
#include "types.hpp"
#include <string>
#include <vector>

int builtin_command_handler(const std::vector<std::string>& args, int user_id);
// bool builtin_command_handler(const std::vector<std::string>& args, int user_id);