#include "parser.hpp"
#include <sstream>
#include <iostream>

void parser(const string& input, vector<CommandGroup>& cmd_groups) {
    stringstream ss(input);
    string token;
    
    CommandGroup group;
    Command cmd;

    while (ss >> token) {
        if (token == "|") {
            cmd.type = OPIPE;
            group.cmds.push_back(cmd);
            cmd = Command();
            continue;
        }
        else if (token == ">") {
            cmd.type = REDIR;
            if (!(ss >> group.redirect_file)) {
                cerr << "Error: syntax error regarding redirection" << endl;
                return;
            }
        }
        else if (token[0] == '|' || token[0] == '!') {
            bool is_num = (token.size() > 1);
            for (size_t i = 1; i < token.size(); ++i) {
                if (!isdigit(token[i])) {
                    is_num = false;
                    break;
                }
            }
            if (is_num) {
                cmd.type = token[0] == '|' ? NPIPE : ERR_NPIPE;
                group.pipe_target = stoi(token.substr(1));

                group.cmds.push_back(cmd);
                cmd = Command();
                cmd_groups.push_back(group);
                group = CommandGroup();
            }
        }
        else cmd.args.push_back(token);
    }

    if (!cmd.args.empty()) group.cmds.push_back(cmd);
    if (!group.cmds.empty()) cmd_groups.push_back(group);
}