//
// Created by Paolo Gastaldi on 28/11/2020.
//

#include <iostream>
#include "../comm/Server_API.h"

int main(int argc, char **argv) {
    if (argc != 6) {
        std::cerr << "Usage: " << argv[0] << " <directory> <host> <port> <username> <password>" << std::endl;
        exit(-1);
    }

    boost::asio::io_context ctx;
    boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::tcp::v4(), std::stoi(argv[3]));
    boost::asio::ip::tcp::acceptor acceptor{ctx, endpoint};
    boost::asio::ip::tcp::socket socket{ctx};
    acceptor.accept(socket);
    auto api = new Server_API{new Socket_API{std::move(socket)}};
    Server_API::set_end([]() { return true; });
    Server_API::set_login([](const std::string &username, const std::string &password) {
        return username == "Bellofigo";
    });
    std::thread t([&ctx]() {
        ctx.run();
    });
    api->run();
    t.join();
    return 0;
}
