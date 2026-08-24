#pragma once
#include <vector>
#include <string>
#include <unistd.h>

using namespace std;
#define MAX_N 1024

enum Type {
    NONE = 0,
    OPIPE = 1, 
    REDIR = 2,  
    NPIPE = 3,
    ERR_NPIPE = 4,
};

struct Command {
    vector<string> args;
    Type type;
    Command() : type(NONE) {}
};

struct CommandGroup {
    vector<Command> cmds;
    int pipe_target;
    string redirect_file;
    CommandGroup() : pipe_target(0) {}
};

struct CommandFD {
    int in;
    int out;
    CommandFD() : in(STDIN_FILENO), out(STDOUT_FILENO) {}
};

struct NumPipeState {
    int read_fd;
    int write_fd;
    bool in_use;
    NumPipeState() : read_fd(STDIN_FILENO), write_fd(STDOUT_FILENO), in_use(false) {}
};

extern NumPipeState pipe_map[MAX_N];
extern int curr_line;