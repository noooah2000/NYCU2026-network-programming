#pragma once
#include "types.hpp"
#include <string>
#include <vector>

using namespace std;

void parser(const string& input, vector<CommandGroup>& cmd_groups);
string validate_single_group(const CommandGroup& group);