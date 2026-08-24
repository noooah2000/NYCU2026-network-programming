#include "parser.hpp"
#include <sstream>
#include <iostream>

string validate_single_group(const CommandGroup& group) {
    for (const auto& cmd : group.cmds) {
        if (cmd.args.empty()) {
            return "Error: invalid null command.\n"; 
        }
    }
    if (group.cmds.back().type == OPIPE) {
        return "Usage: There must need a command after '|'.\n";
    }
    if (group.cmds.back().type == REDIR && group.redirect_file.empty()) {
        return "Usage: Missing redirection file.\n";
    }
    return "";
}

void parser(const string& input, vector<CommandGroup>& cmd_groups) {
    stringstream ss(input);
    string token;
    
    CommandGroup group;
    Command cmd;

    bool is_first_token = true;
    while (ss >> token) {
        if (is_first_token && (token == "yell" || token == "tell")) {
            cmd.args.push_back(token);
            if (token == "tell") {
                string id;
                if (ss >> id) cmd.args.push_back(id);
            }

            string msg;
            getline(ss, msg);
            if (!msg.empty() && msg[0] == ' ') msg.erase(0, 1);
            if (!msg.empty()) cmd.args.push_back(msg);
            group.cmds.push_back(cmd);
            cmd_groups.push_back(group);
            return;
        }

        if (token == "|") {
            cmd.type = OPIPE;
            group.cmds.push_back(cmd);
            cmd = Command();
        }
        else if (token == ">") {
            cmd.type = REDIR;
            ss >> group.redirect_file;
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
        else if (token[0] == '>' || token[0] == '<') {
            bool is_num = (token.size() > 1);
            for (size_t i = 1; i < token.size(); ++i) {
                if (!isdigit(token[i])) {
                    is_num = false;
                    break;
                }
            }
            if (is_num) {
                if (token[0] == '<') group.user_pipe_in = stoi(token.substr(1));
                else if (token[0] == '>') group.user_pipe_out = stoi(token.substr(1));

            }
        }
        else cmd.args.push_back(token);
        is_first_token = false;
    }

    if (!cmd.args.empty()) group.cmds.push_back(cmd);
    if (!group.cmds.empty()) cmd_groups.push_back(group);
}