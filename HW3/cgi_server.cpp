#include <iostream>
#include <sstream>
#include <cstdlib>
#include <string>
#include <memory>
#include <vector>
#include <fstream>
#include <queue>
#include <boost/asio.hpp>
#include <boost/algorithm/string.hpp>

using boost::asio::ip::tcp;
class HttpSession;
struct TargetServerInfo {
    std::string host;
    std::string port;
    std::string file;
};

class Client : public std::enable_shared_from_this<Client> {
public:
    Client(boost::asio::io_context& io_context, int id, const TargetServerInfo& info, 
            std::shared_ptr<HttpSession> http_session)
        : resolver_(io_context), conn_sock_(io_context), id_(id), info_(info), http_session_(http_session) {
        
        std::string filepath = "test_case/" + info_.file;
        file_stream_.open(filepath);
    }

    void start() {
        do_resolve();
    }

private:
    tcp::resolver resolver_;
    tcp::socket conn_sock_;
    int id_;
    TargetServerInfo info_;
    std::shared_ptr<HttpSession> http_session_; 
    std::ifstream file_stream_;

    static constexpr int max_length = 4096;
    char data_[max_length];
 
    std::string shell_response_buffer_;
    std::string shell_command_buffer_;

    void do_resolve() {
        auto self(shared_from_this());
        resolver_.async_resolve(info_.host, info_.port,
        [this, self](boost::system::error_code ec, tcp::resolver::results_type results) {
            if (!ec) do_connect(results);
        });
    }

    void do_connect(tcp::resolver::results_type results) {
        auto self(shared_from_this());
        boost::asio::async_connect(conn_sock_, results, 
        [this, self](boost::system::error_code ec, tcp::endpoint) {
            if (!ec) do_read_shell_response();
        });
    }

    void do_read_shell_response() {
        auto self(shared_from_this());
        conn_sock_.async_read_some(boost::asio::buffer(data_, max_length),
        [this, self](boost::system::error_code ec, std::size_t length){
            if (!ec) {
                std::string shell_response(data_, length);
                do_print_shell_response(shell_response);
            }
        });
    }

    void do_print_shell_response(const std::string& shell_response);
    void do_read_file_and_print_shell_command();

    void do_give_shell_command(const std::string& shell_command) {
        auto self(shared_from_this());
        shell_command_buffer_ = shell_command;

        boost::asio::async_write(conn_sock_, boost::asio::buffer(shell_command_buffer_),
        [this, self](boost::system::error_code ec, std::size_t) {
            if (!ec) do_read_shell_response(); 
        });
    }

    void escape_html(std::string& data) {
        boost::replace_all(data, "&", "&amp;");
        boost::replace_all(data, "\"", "&quot;");
        boost::replace_all(data, "\'", "&apos;");
        boost::replace_all(data, "<", "&lt;");
        boost::replace_all(data, ">", "&gt;");
        boost::replace_all(data, "\n", "&NewLine;");
        boost::replace_all(data, "\r", "");
    }
};


class HttpSession : public std::enable_shared_from_this<HttpSession> {
public:
    HttpSession(tcp::socket socket, boost::asio::io_context& io) 
        : accept_socket_(std::make_shared<tcp::socket>(std::move(socket))), io_context_(io) {}

    void start() {
        do_read();
    }

    void send_to_browser(std::string msg) {
        output_queue_.push(std::move(msg));
        if (!is_writing_) {
            do_write_to_browser();
        }
    }

private:
    std::queue<std::string> output_queue_;
    bool is_writing_ = false;

    std::shared_ptr<tcp::socket> accept_socket_;
    boost::asio::io_context& io_context_;
    static constexpr int max_length = 1024;
    char data_[max_length];
    std::string request_buffer_;
    std::string html_buffer_;
    
    std::string request_uri_;
    std::string query_string_;
    std::vector<TargetServerInfo> sessions_;

    void do_write_to_browser() {
        auto self(shared_from_this());
        is_writing_ = true;
        
        boost::asio::async_write(*accept_socket_, boost::asio::buffer(output_queue_.front()),
        [this, self](boost::system::error_code ec, std::size_t) {
            if (!ec) {
                output_queue_.pop();
                
                if (!output_queue_.empty()) {
                    do_write_to_browser();
                } 
                else {
                    is_writing_ = false;
                }
            }
        });
    }

    void do_read() {
        auto self(shared_from_this());
        accept_socket_->async_read_some(boost::asio::buffer(data_, max_length),
        [this, self](boost::system::error_code ec, std::size_t length) {
            if (!ec) {
                request_buffer_.append(data_, length);
                if (request_buffer_.find("\r\n\r\n") != std::string::npos) {
                    parse_request();
                } else {
                    do_read();
                }
            }
        });
    }

    void parse_request() {
        std::stringstream ss(request_buffer_);
        std::string method, protocol;

        ss >> method >> request_uri_ >> protocol;

        size_t pos;
        if ((pos = request_uri_.find("?")) != std::string::npos) {
            query_string_ = request_uri_.substr(pos + 1);
            request_uri_ = request_uri_.substr(0, pos);
        }

        std::cerr << "Method: " << method << std::endl;
        std::cerr << "URI: " << request_uri_ << std::endl;
        std::cerr << "Query: " << query_string_ << std::endl;
        std::cerr << "----------------------------------------" << std::endl;

        if (request_uri_ == "/panel.cgi") {
            do_panel();
        } 
        else if (request_uri_ == "/console.cgi") {
            do_console();
        }
    }

    void do_panel() {
        html_buffer_.clear();
        html_buffer_ += "HTTP/1.1 200 OK\r\n";
        html_buffer_ += "Content-type: text/html\r\n\r\n";
        html_buffer_ += R"(
        <!DOCTYPE html>
        <html lang="en">
            <head>
                <title>NP Project 4 Panel</title>
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
                    href="https://cdn4.iconfinder.com/data/icons/iconsimple-setting-time/512/dashboard-512.png"
                />
                <style>
                    * {
                        font-family: 'Source Code Pro', monospace;
                    }
                </style>
            </head>
            <body class="bg-secondary pt-5">
                <form action="console.cgi" method="GET">
                    <table class="table mx-auto bg-light" style="width: inherit">
                        <thead class="thead-dark">
                            <tr>
                                <th scope="col">#</th>
                                <th scope="col">Host</th>
                                <th scope="col">Port</th>
                                <th scope="col">Input File</th>
                            </tr>
                        </thead>
                        <tbody>
        )";
        
        std::string host_menu = "";
        for (int i = 1; i <= 12; ++i) {
            std::string host = "nplinux" + std::to_string(i);
            host_menu += "<option value=\"" + host + ".cs.nycu.edu.tw\">" + host + "</option>\n";
        }
        for (int i = 0; i < 5; ++i) {
            html_buffer_ += R"(
                            <tr>
                                <th scope="row" class="align-middle">Session )" + std::to_string(i + 1) + R"(</th>
                                <td>
                                    <div class="input-group">
                                            <select name="h)" + std::to_string(i) + R"(" class="custom-select">
                                                <option></option>)" +
                                                host_menu + R"(
                                            </select>
                                        <div class="input-group-append">
                                            <span class="input-group-text">.cs.nycu.edu.tw</span>
                                        </div>
                                    </div>
                                </td>
                                <td>
                                    <input name="p)" + std::to_string(i) + R"(" type="text" class="form-control" size="5" />
                                </td>
                                <td>
                                    <select name="f)" + std::to_string(i) + R"(" class="custom-select">
                                        <option></option>
                                        <option value="t1.txt">t1.txt</option>
                                        <option value="t2.txt">t2.txt</option>
                                        <option value="t3.txt">t3.txt</option>
                                        <option value="t4.txt">t4.txt</option>
                                        <option value="t5.txt">t5.txt</option>
                                    </select>
                                </td>
                            </tr>
            )";
        }
        html_buffer_ += R"(
                            <tr>
                                <td colspan="3"></td>
                                <td>
                                    <button type="submit" class="btn btn-info btn-block">Run</button>
                                </td>
                            </tr>
                        </tbody>
                    </table>
                </form>
            </body>
        </html>
        )";
        
        auto self(shared_from_this());
        boost::asio::async_write(*accept_socket_, boost::asio::buffer(html_buffer_),
        [this, self](boost::system::error_code ec, std::size_t) {
            accept_socket_->close();
        });
    }

    void do_console() {
        parse_query();
        html_buffer_.clear();
        html_buffer_ += "HTTP/1.1 200 OK\r\n";
        html_buffer_ += "Content-type: text/html\r\n\r\n";
        html_buffer_ += R"(
        <!DOCTYPE html>
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
                        <tr>
        )"; 

        for (size_t i = 0; i < sessions_.size(); ++i) {
            html_buffer_ += "<th scope=\"col\">" + sessions_[i].host + ":" + sessions_[i].port + "</th>\n";
        }

        html_buffer_ += R"(
                        </tr>
                    </thead>
                    <tbody>
                        <tr>
        )";

        for (size_t i = 0; i < sessions_.size(); ++i) {
            html_buffer_ += "<td><pre id=\"s" + std::to_string(i) + "\" class=\"mb-0\"></pre></td>\n";
        }

        html_buffer_ += R"(
                        </tr>
                    </tbody>
                </table>
            </body>
        </html>
        )";

        auto self(shared_from_this());
        boost::asio::async_write(*accept_socket_, boost::asio::buffer(html_buffer_),
        [this, self](boost::system::error_code ec, std::size_t) {
            if (!ec) {
                for (size_t i = 0; i < sessions_.size(); ++i) {
                    std::make_shared<Client>(io_context_, i, sessions_[i], self)->start();
                }
            }
        });
    }

    void parse_query() {
        std::string h[5], p[5], f[5];
        
        size_t start_pos = 0;
        while (start_pos < query_string_.size()) {
            size_t end_pos = query_string_.find('&', start_pos);
            if (end_pos == std::string::npos) end_pos = query_string_.size();

            std::string token = query_string_.substr(start_pos, end_pos - start_pos);
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
        sessions_.clear();
        for (int i = 0; i < 5; ++i) {
            if (!h[i].empty() && !p[i].empty() && !f[i].empty()) {
                sessions_.push_back({h[i], p[i], f[i]});
            }
        }
    }
};

void Client::do_print_shell_response(const std::string& shell_response) {
    auto self(shared_from_this());
    
    std::string html_msg =shell_response;
    escape_html(html_msg);
    html_msg = "<script>document.getElementById('s" + 
        std::to_string(id_) + "').innerHTML += '" + html_msg + "';</script>";
    http_session_->send_to_browser(std::move(html_msg));

    shell_response_buffer_ += shell_response;
    if (shell_response_buffer_.find("% ") != std::string::npos) {
        shell_response_buffer_.clear();
        do_read_file_and_print_shell_command();
    } 
    else {
        do_read_shell_response();
    }
    
}

void Client::do_read_file_and_print_shell_command() {
    auto self(shared_from_this());
    std::string shell_command;

    if (std::getline(file_stream_, shell_command)) {
        shell_command += "\n";

        std::string html_msg = shell_command;
        escape_html(html_msg);
        html_msg = "<script>document.getElementById('s" + 
            std::to_string(id_) + "').innerHTML += '<b>" + html_msg + "</b>';</script>";
        http_session_->send_to_browser(std::move(html_msg));

        do_give_shell_command(shell_command);
    }
}

class Server {
public:
    Server(boost::asio::io_context& io_context, short port) 
        : acceptor_(io_context, tcp::endpoint(tcp::v4(), port)), io_context_(io_context) {
        do_accept();
    }
private:
    tcp::acceptor acceptor_;
    boost::asio::io_context& io_context_;

    void do_accept() {
        acceptor_.async_accept(
        [this](boost::system::error_code ec, tcp::socket socket) {
            if (!ec) {
                std::make_shared<HttpSession>(std::move(socket), io_context_)->start();
            }
            do_accept();
        });
    }
};


int main(int argc, char* argv[]) {
    try {
        if (argc != 2) {
            std::cerr << "Usage: cgi_server <port>\n";
            return 1;
        }

        boost::asio::io_context io_context;
        Server s(io_context, std::atoi(argv[1]));
        io_context.run();
    }
    catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }
}