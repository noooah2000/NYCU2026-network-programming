#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <csignal>
#include <boost/asio.hpp>
#include <boost/algorithm/string.hpp>

using boost::asio::ip::tcp;

struct TargetServerInfo {
    std::string host;
    std::string port;
    std::string file;
};
std::vector<TargetServerInfo> server_info;

void escape_html(std::string& data) {
    boost::replace_all(data, "&", "&amp;");
    boost::replace_all(data, "\"", "&quot;");
    boost::replace_all(data, "\'", "&apos;");
    boost::replace_all(data, "<", "&lt;");
    boost::replace_all(data, ">", "&gt;");
    boost::replace_all(data, "\n", "&NewLine;");
    boost::replace_all(data, "\r", "");
}

void output_shell_response(int id, std::string content) {
    escape_html(content);
    std::cout << "<script>document.getElementById('s" << id << 
    "').innerHTML += '" << content << "';</script>" << std::flush;
}

void output_shell_command(int id, std::string content) {
    escape_html(content);
    std::cout << "<script>document.getElementById('s" << id << 
    "').innerHTML += '<b>" << content << "</b>';</script>" << std::flush;
}

void parse_query_string() {
    const char* qs_env = getenv("QUERY_STRING");
    std::string query_string(qs_env ? qs_env : "");
    std::string h[5], p[5], f[5];
    
    size_t start_pos = 0;
    while (start_pos < query_string.size()) {
        size_t end_pos = query_string.find('&', start_pos);
        if (end_pos == std::string::npos) end_pos = query_string.size();

        std::string token = query_string.substr(start_pos, end_pos - start_pos);
        size_t equal_pos = token.find('=');
        if (equal_pos != std::string::npos) {
            std::string key = token.substr(0, equal_pos);
            std::string value = token.substr(equal_pos + 1);

            if (key.size() == 2) {
                int idx = key[1] - '0';
                if (0 <= idx && idx <= 4) {
                    if (key[0] == 'h') h[idx] = value;
                    else if (key[0] == 'p') p[idx] = value;
                    else if (key[0] == 'f') f[idx] = value;
                }
            }
        }
        start_pos = end_pos + 1;
    }
    for (int i = 0; i < 5; ++i) {
        if (!h[i].empty() && !p[i].empty() && !f[i].empty()) {
            server_info.push_back({h[i], p[i], f[i]});
        }
    }
}

void generate_html_layout() {
    std::cout << "Content-type: text/html\r\n\r\n";

    std::cout <<
    R"(<!DOCTYPE html>
    <html lang="en">
        <head>
            <meta charset="UTF-8" />
            <title>NP Project 3 Console</title>
            <link
                rel="stylesheet"
                href="https://cdn.jsdelivr.net/npm/bootstrap@4.5.3/dist/css/bootstrap.min.css"
                integrity="sha384-TX8t27EcRE3e/ihU7zmQxVncDAy5uIKz4rEkgIXeMed4M0jlfIDPvg6uqKI2xXr2"
                crossorigin="anonymous"
            />
            <link
                href="https://fonts.googleapis.com/css?family=Source+Code+Pro"
                rel="stylesheet"
            />
            <link
                rel="icon"
                type="image/png"
                href="https://cdn0.iconfinder.com/data/icons/small-n-flat/24/678068-terminal-512.png"
            />
            <style>
                * {
                    font-family: 'Source Code Pro', monospace;
                    font-size: 1rem !important;
                }
                body {
                    background-color: #212529;
                }
                pre {
                    color: #cccccc;
                }
                b {
                    color: #01b468;
                }
            </style>
        </head>
        <body>
            <table class="table table-dark table-bordered">
                <thead>
                    <tr>)" 
        << std::endl;
    
    for (size_t i = 0; i < server_info.size(); ++i) {
        std::cout << "<th scope=\"col\">" << server_info[i].host << ":" << server_info[i].port << "</th>" << std::endl;
    }

    std::cout <<
                    R"(</tr>
                </thead>
                <tbody>
                    <tr>)"
    << std::endl;

    for (size_t i = 0; i < server_info.size(); ++i) {
        std::cout << "<td><pre id=\"s" << i << "\" class=\"mb-0\"></pre></td>" << std::endl;
    }

    std::cout <<
                    R"(</tr>
                </tbody>
            </table>
        </body>
    </html>)"
    << std::endl;
}

class Client : public std::enable_shared_from_this<Client> {
public:
    Client(boost::asio::io_context& io_context, int id, const TargetServerInfo& info)
        : resolver_(io_context), conn_socket_(io_context), id_(id), info_(info) {
        
        std::string filepath = "test_case/" + info_.file;
        file_stream_.open(filepath);
    }

    void start() {
        do_resolve();
    }
private:
    tcp::resolver resolver_;
    tcp::socket conn_socket_;
    int id_;
    TargetServerInfo info_;
    std::ifstream file_stream_;
    std::string shell_response_buffer_;

    static constexpr int max_length = 4096;
    char data_[max_length];

    void do_resolve() {
        auto self(shared_from_this());
        resolver_.async_resolve(info_.host, info_.port,
        [this, self](boost::system::error_code ec, tcp::resolver::results_type results) {
            if (!ec) do_connect(results);
        });
    }

    void do_connect(tcp::resolver::results_type results) {
        auto self(shared_from_this());
        boost::asio::async_connect(conn_socket_, results, 
        [this, self](boost::system::error_code ec, tcp::endpoint) {
            if (!ec) do_read();
            else {
                std::string err_msg = "Connect failed: " + ec.message() + "\n";
                output_shell_response(id_, err_msg);
            }
        });
    }

    void do_read() {
        auto self(shared_from_this());
        conn_socket_.async_read_some(boost::asio::buffer(data_, max_length),
        [this, self](boost::system::error_code ec, std::size_t length){
            if (!ec) {
                std::string shell_response(data_, length);
                output_shell_response(id_, shell_response); 
                shell_response_buffer_.append(data_, length);

                if (shell_response_buffer_.find("% ") != std::string::npos) {
                    shell_response_buffer_.clear();
                    do_write();
                } 
                else {
                    do_read();
                }
            }
        });
    }

    void do_write() {
        auto self(shared_from_this());
        std::string shell_command;

        if (std::getline(file_stream_, shell_command)) {
            shell_command += "\n";
            output_shell_command(id_, shell_command);

            boost::asio::async_write(conn_socket_, boost::asio::buffer(shell_command),
            [this, self](boost::system::error_code ec, std::size_t) {
                if (!ec) do_read(); 
            });
        }
    }
};

int main() {
    parse_query_string();
    generate_html_layout();
    try {
        signal(SIGPIPE, SIG_IGN);
        boost::asio::io_context io_context;
        for (size_t i = 0; i < server_info.size(); ++i) {
            std::make_shared<Client>(io_context, i, server_info[i])->start();
        }
        io_context.run();
    }
    catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }
}