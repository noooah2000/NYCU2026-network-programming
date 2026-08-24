#pragma once
#include <vector>
#include <string>
#include <map>
#include <unistd.h>

using namespace std;
#define MAX_N 1024
#define MAX_USERS 30

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
    
    int user_pipe_in;
    int user_pipe_out;
    CommandGroup() : pipe_target(0), user_pipe_in(0), user_pipe_out(0) {}
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
struct UserPipeState {
    int read_fd;
    int write_fd;
    bool in_use;
    UserPipeState() : read_fd(-1), write_fd(-1), in_use(false) {}
};

struct UserPipeTable {
    UserPipeState state[MAX_USERS + 1][MAX_USERS + 1];
    void clear(int user_id) {
        for (int i = 1; i <= MAX_USERS; ++i) {
            if (state[i][user_id].in_use) {
                close(state[i][user_id].read_fd);
                close(state[i][user_id].write_fd);
                state[i][user_id] = UserPipeState();
            }
            if (state[user_id][i].in_use) {
                close(state[user_id][i].read_fd);
                close(state[user_id][i].write_fd);
                state[user_id][i] = UserPipeState();
            }
        }
    }
};

struct UserInfo {
    bool is_active = false;
    int fd = -1;
    string nickname = "(no name)";
    string ip = "";
    int port = 0;
    map<string, string> env;
    string cmd_buffer = "";

    int curr_line;
    NumPipeState pipe_map[MAX_N];

    void reset() {
        is_active = false;
        if (fd != -1) {
            close(fd);
            fd = -1;
        }
        nickname = "(no name)";
        ip = "";
        port = 0;
        env.clear();
        env["PATH"] = "bin:.";
        cmd_buffer.clear();

        curr_line = 0;
        for (int i = 0; i < MAX_N; ++i) {
            pipe_map[i] = NumPipeState();
        }
    }
};

extern UserInfo users[MAX_USERS + 1];
extern UserPipeTable user_pipe_table;


void broadcast(const string& msg);
struct SharedUserInfo {
    bool is_active;
    int fd = -1; // 單純給who印出來看的
    char nickname[22];
    char ip[17];
    int port;
    pid_t pid;
};

struct SharedMemory {
    SharedUserInfo users[MAX_USERS + 1];
    char broadcast_msg[1026];
};

extern SharedMemory* shm;