#include "executor.hpp"
#include <iostream>
#include <cstdlib>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/socket.h>

using namespace std;

void external_command(vector<string> args) {
    vector<char*> c_args;
    for (const auto& s : args) {
        c_args.push_back(const_cast<char*>(s.c_str()));
    }
    c_args.push_back(nullptr);
    if (execvp(c_args[0], c_args.data()) == -1) {
        cerr << "Unknown command: [" << args[0] << "]." << endl;
        exit(0);
    }
}

void redirection(CommandFD& curr_fd, string& redirect_file, int user_id) {
    curr_fd.out = open(redirect_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (curr_fd.out < 0) {
        perror("open error");
        string err_msg = "open error\n";
        send(users[user_id].fd, err_msg.c_str(), err_msg.length(),  MSG_NOSIGNAL);
    }
}

void ordinary_pipe(CommandFD& curr_fd, CommandFD& next_fd) {
    int fd[2];
    pipe(fd);
    curr_fd.out = fd[1];
    next_fd.in = fd[0];
}

void numbered_pipe(CommandFD& curr_fd, int pipe_target, NumPipeState* pipe_map, int curr_line) {
    int N = (curr_line + pipe_target) % MAX_N;
    NumPipeState& target_NPS = pipe_map[N];
    if (!target_NPS.in_use) {
        int fd[2];
        pipe(fd);
        target_NPS.write_fd = fd[1];
        target_NPS.read_fd = fd[0];
        target_NPS.in_use = true;
    }
    curr_fd.out = target_NPS.write_fd;
}

int user_pipe_in(int user_id, int from_id, const string& input) {
    string err_msg;
    if (from_id > MAX_USERS ||!users[from_id].is_active) {
        err_msg = "*** Error: user #" + to_string(from_id) + " does not exist yet. ***\n";
        send(users[user_id].fd, err_msg.c_str(), err_msg.length(),  MSG_NOSIGNAL);
        return open("/dev/null", O_RDONLY); 
    }
    else if (!user_pipe_table.state[from_id][user_id].in_use) {
        err_msg = "*** Error: the pipe #" + to_string(from_id) + "->#" + to_string(user_id) + " does not exist yet. ***\n";
        send(users[user_id].fd, err_msg.c_str(), err_msg.length(),  MSG_NOSIGNAL);
        return open("/dev/null", O_RDONLY); 
    }

    int read_fd = user_pipe_table.state[from_id][user_id].read_fd;
    user_pipe_table.state[from_id][user_id] = UserPipeState();
    
    string msg;
    for (int i = 1; i <= MAX_USERS; ++i) {
        if (!users[i].is_active) continue;
        msg = "*** " + users[user_id].nickname + " (#" + to_string(user_id) + ") just received from " + 
                    users[from_id].nickname + " (#" + to_string(from_id) + ") by '" + input+ "' ***\n";
        send(users[i].fd, msg.c_str(), msg.length(),  MSG_NOSIGNAL);
    }
    return read_fd;
}

int user_pipe_out(int user_id, int to_id, const string& input) {
    string err_msg;
    if (to_id > MAX_USERS ||!users[to_id].is_active) {
        err_msg = "*** Error: user #" + to_string(to_id) + " does not exist yet. ***\n";
        send(users[user_id].fd, err_msg.c_str(), err_msg.length(),  MSG_NOSIGNAL);
        return open("/dev/null", O_WRONLY); 
    }
    else if (user_pipe_table.state[user_id][to_id].in_use) {
        err_msg = "*** Error: the pipe #" + to_string(user_id) + "->#" + to_string(to_id) + " already exists. ***\n";
        send(users[user_id].fd, err_msg.c_str(), err_msg.length(),  MSG_NOSIGNAL);
        return open("/dev/null", O_WRONLY); 
    }
    

    int fd[2];
    pipe(fd);
    user_pipe_table.state[user_id][to_id].write_fd = fd[1];
    user_pipe_table.state[user_id][to_id].read_fd = fd[0];
    user_pipe_table.state[user_id][to_id].in_use = true;
    
    string msg;
    for (int i = 1; i <= MAX_USERS; ++i) {
        if (!users[i].is_active) continue;
        msg = "*** " + users[user_id].nickname + " (#" + to_string(user_id) + ") just piped '" + input+ "' to " + 
                    users[to_id].nickname + " (#" + to_string(to_id) + ") ***\n";
        send(users[i].fd, msg.c_str(), msg.length(),  MSG_NOSIGNAL);
    }
    return fd[1];
}

void execute_child(Command& cmd, int pipe_target, CommandFD& curr_fd, CommandFD& next_fd, NumPipeState* pipe_map, int curr_line) {
    if (curr_fd.in != STDIN_FILENO) {
        dup2(curr_fd.in, STDIN_FILENO);
        close(curr_fd.in);
    }
    if (curr_fd.out != STDOUT_FILENO) {
        dup2(curr_fd.out, STDOUT_FILENO);
        if (cmd.type == ERR_NPIPE) {
            dup2(curr_fd.out, STDERR_FILENO);
        }
        close(curr_fd.out);
    }
    if (next_fd.in != STDIN_FILENO) close(next_fd.in);
    if (cmd.type >= NPIPE) {
        int N = (curr_line + pipe_target) % MAX_N;
        NumPipeState& target_NPS = pipe_map[N];
        close(target_NPS.read_fd);
    }
    external_command(cmd.args);
}

void execute_parent(Command& cmd, CommandFD& curr_fd, vector<pid_t>& children, pid_t& child_pid) {
    if (curr_fd.in != STDIN_FILENO) close(curr_fd.in);
    if (curr_fd.out != STDOUT_FILENO && (cmd.type < NPIPE)) close(curr_fd.out);
    if (cmd.type < NPIPE) children.push_back(child_pid);
}

void execute_pipeline(CommandGroup& group, int user_id, const string& input) {
    int& curr_line = users[user_id].curr_line;
    int client_fd = users[user_id].fd;
    NumPipeState* pipe_map = users[user_id].pipe_map;

    CommandFD curr_fd;
    vector<Command>& cmds = group.cmds;
    NumPipeState& curr_NPS = pipe_map[curr_line % MAX_N];
    if (group.user_pipe_in != 0) {
        if (curr_NPS.in_use) {
            close(curr_NPS.read_fd);
            close(curr_NPS.write_fd);
            curr_NPS.in_use = false;
        }
    } 
    else if (curr_NPS.in_use == true) {
        curr_fd.in = curr_NPS.read_fd;
        close(curr_NPS.write_fd);
    }

    int up_in_fd;
    int up_out_fd;
    if (group.user_pipe_in != 0) {
        up_in_fd = user_pipe_in(user_id, group.user_pipe_in, input);
    }
    if (group.user_pipe_out != 0) {
        up_out_fd = user_pipe_out(user_id, group.user_pipe_out, input);
    }

    vector<pid_t> children;
    for (size_t i = 0; i < cmds.size(); ++i) {
        CommandFD next_fd;

        if (i == 0 && group.user_pipe_in != 0) {
            curr_fd.in = up_in_fd;
        }
        if (i == (cmds.size()-1) && group.user_pipe_out != 0) {
            curr_fd.out = up_out_fd;
        }
        
        if (cmds[i].type == OPIPE) {
            ordinary_pipe(curr_fd, next_fd);
        }
        else if (cmds[i].type == REDIR) {
            redirection(curr_fd, group.redirect_file, user_id);
        }
        else if (cmds[i].type == NPIPE || cmds[i].type == ERR_NPIPE) {
            numbered_pipe(curr_fd, group.pipe_target, pipe_map, curr_line);
        }

        pid_t pid;
        while ((pid = fork()) < 0) {
            if (errno == EAGAIN) {
                waitpid(-1, nullptr, 0);
                while (waitpid(-1, nullptr, WNOHANG) > 0); // Non-blocking
            }
            else {
                perror("fork error");
                exit(1);
            }
        }
        if (pid == 0) {
            dup2(client_fd, STDOUT_FILENO);
            dup2(client_fd, STDERR_FILENO);
            execute_child(cmds[i], group.pipe_target, curr_fd, next_fd, pipe_map, curr_line);
        }
        else { // pid > 0
            execute_parent(cmds[i], curr_fd, children, pid);
        }
        curr_fd = next_fd;
    }
    if (curr_NPS.in_use == true) {
        curr_NPS.in_use = false;
    }
    if (group.pipe_target != 0 || group.user_pipe_out != 0) return; // prevent deadlock which cause by big file
    for (pid_t child : children) {
        waitpid(child, nullptr, 0);
    }
}