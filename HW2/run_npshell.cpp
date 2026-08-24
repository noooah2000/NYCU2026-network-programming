#include <iostream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include "types.hpp"
#include "parser.hpp"
#include "builtin.hpp"
#include "executor.hpp"

using namespace std;

NumPipeState pipe_map[MAX_N];
int curr_line = 0;

void run_npshell(int user_id){
    clearenv();
    setenv("PATH", "bin:.", 1);

    string input;
    for(;;) {
        while (waitpid(-1, nullptr, WNOHANG) > 0); // 有些number pipe所留下的 zombie在這時候清掉
        cout << "% " << flush;

        if (!getline(cin, input)) break;

        if (!input.empty() && input.back() == '\r') input.pop_back(); 
        if (input.empty()) continue;
        
        vector<CommandGroup> cmd_groups;
        parser(input, cmd_groups);

        if (cmd_groups.empty()) continue;
        
        for (CommandGroup& group : cmd_groups) {
            if (group.cmds.empty()) continue;
            string err_msg = validate_single_group(group);
            if (!err_msg.empty()) {
                cerr << err_msg << flush;
                continue;
            }
            ++curr_line; 

            int builtin_status = builtin_command_handler(group.cmds[0].args, user_id);
            if (builtin_status < 0) {
                return;
            }
            else if (builtin_status > 0) {
                NumPipeState& curr_NPS = pipe_map[curr_line % MAX_N];
                if (curr_NPS.in_use == true) {
                    close(curr_NPS.read_fd);
                    close(curr_NPS.write_fd);
                    curr_NPS.in_use = false;
                }
                continue;
            } 
            
            execute_pipeline(group, user_id, input);
        }
    }
}