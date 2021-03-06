//
// Created by stevezbiz on 26/11/20.
//

#include "Socket_API.h"

#include <utility>

void Socket_API::init_config_() {
    boost::asio::ip::tcp::resolver resolver(ctx_);
    Logger::info("Socket_API::init_config_", "Resolving to " + ip + ":" + port, PR_VERY_LOW);
    endpoint_iterator_ = resolver.resolve({ ip, port });
}

bool Socket_API::call_(const std::function<void(boost::asio::ip::tcp::socket &, boost::system::error_code &)> &perform_this) {
    bool status = false;
    bool stop = false;
    int retry_cont = 0;
    boost::system::error_code ec;

    try {
        while (!stop && retry_cont <= n_retry_) {
            if(retry_cont > 0)
                Logger::warning("Socket_API::call_", "Retry " + std::to_string(retry_cont));
            if(!(socket_ && socket_->is_open())) {
                Logger::warning("Socket_API::call_", "Invalid socket");
                comm_error = std::make_shared<Comm_error>(CE_FAILURE, "Socket_API::call_", "Invalid socket");
                break;
            }

            perform_this(*socket_, ec);

            if(!ec.failed()) {
                stop = true;
                status = true;
                ec.clear();
            } else {
                comm_error = std::make_shared<Comm_error>(CE_FAILURE, "Socket_API::call_", "Operation failure on socket", ec);
                std::this_thread::sleep_for(std::chrono::milliseconds(retry_delay_));
                retry_cont++;
            }
        }
    } catch(const std::exception &ec) {
        comm_error = std::make_shared<Comm_error>(CE_GENERIC, "Socket_API::call_", "Exception caught: " + std::string{ ec.what() }); // never used
        return false;
    }

    return status;
}

bool Socket_API::generic_handler_(const boost::system::error_code &ec, std::size_t bytes_transferred) {
    std::cout << ec << " errors, " <<bytes_transferred << " bytes as been transferred correctly" << std::endl;
    return ec.value();
}

bool Socket_API::receive_header_() {
    if(!call_([this](boost::asio::ip::tcp::socket &socket, boost::system::error_code &ec) {
            boost::asio::read(socket, message->get_header_buffer(), ec);
        })) {
        comm_error = std::make_shared<Comm_error>(CE_FAILURE, "Socket_API::receive_header_", "Unable to read message header");
        return false;
    }

    return true;
}

bool Socket_API::receive_content_() {
    if(!call_([this](boost::asio::ip::tcp::socket &socket, boost::system::error_code &ec) {
            boost::asio::read(socket, message->get_content_buffer(), ec);
        })) {
        comm_error = std::make_shared<Comm_error>(CE_FAILURE, "Socket_API::receive_content_", "Unable to read message content");
        return false;
    }

    return true;
}

Socket_API::Socket_API(std::string ip, std::string port, ERROR_MANAGEMENT error_management, long retry_delay, bool keep_alive) :
        ip(std::move(ip)),
        port(std::move(port)),
        n_retry_(error_management),
        retry_delay_(retry_delay),
        keep_alive(keep_alive) {
    init_config_();
}

Socket_API::Socket_API(boost::asio::ip::tcp::socket socket, ERROR_MANAGEMENT error_management, long retry_delay, bool keep_alive) :
        n_retry_(error_management),
        retry_delay_(retry_delay),
        keep_alive(keep_alive) {
    socket_ = std::make_unique<boost::asio::ip::tcp::socket>(std::move(socket));
}

bool Socket_API::open_conn(bool force) {
    Logger::info("Socket_API::open_conn", "Opening a connection...", PR_VERY_LOW);

    if(socket_ != nullptr && socket_->is_open() && keep_alive && !force) {
        Logger::info("Socket_API::open_conn", "Connection already opened", PR_VERY_LOW);
        return true;
    }

    if(!close_conn() || ctx_.stopped()) // close current socket in a safe way
        return false;

    auto f = std::async(std::launch::async, [this]() {
        try {
            Logger::info("Socket_API::open_conn", "Creating the socket...", PR_VERY_LOW);
            Logger::info("Socket_API::open_conn", "Opening socket", PR_VERY_LOW);
//                auto socket = new boost::asio::ip::tcp::socket{ ctx_ };
            boost::asio::ip::tcp::socket socket{ ctx_ };
            Logger::info("Socket_API::open_conn", "Connecting", PR_VERY_LOW);
            boost::asio::connect(socket, endpoint_iterator_);
            Logger::info("Socket_API::open_conn", "Moving the socket", PR_VERY_LOW);
//                socket_ = std::unique_ptr<boost::asio::ip::tcp::socket>(socket);
            socket_ = std::make_unique<boost::asio::ip::tcp::socket>(std::move(socket));
            Logger::info("Socket_API::open_conn", "Everything is done", PR_VERY_LOW);
        } catch(const std::exception &e) {
            comm_error = std::make_shared<Comm_error>(CE_FAILURE, "Socket_API::open_conn", e.what());
            return false;
        }
        return true;
    });
    auto future_status = f.wait_for(std::chrono::milliseconds(CONN_TIMEOUT));
    if(future_status == std::future_status::timeout) {
        comm_error = std::make_shared<Comm_error>(CE_FAILURE, "Socket_API::open_conn", "Unable to connect, timeout");
        return false;
    }
    Logger::info("Socket_API::open_conn", "Opening a connection... - done", PR_VERY_LOW);

    return f.get();
}

bool Socket_API::send(std::shared_ptr<Message> msg) {
    Logger::info("Socket_API::send", "Sending a msg...", PR_LOW);

    msg->keep_alive = keep_alive;
    Logger::info("Socket_API::send", "Sending a message with keep alive " + std::to_string(keep_alive), PR_LOW);
    if(!call_([&msg](boost::asio::ip::tcp::socket &socket, boost::system::error_code &ec) {
            boost::asio::write(socket, msg->send(), ec);
        })) {
        comm_error = std::make_shared<Comm_error>(CE_FAILURE, "Socket_API::send", "Unable to write msg");
        return false;
    }
    Logger::info("Socket_API::send", "Sending a msg... - done", PR_LOW);

    return true;
}

bool Socket_API::receive(MESSAGE_TYPE expectedMessage) {
    bool status = true;
    message = std::make_shared<Message>();
    Logger::info("Socket_API::receive", "Receiving a message...", PR_LOW);

    if(!receive_header_())
        return false; // it cannot do any other action here

    try {
        message = message->build_header(); // build the header
    } catch(const std::exception &ec) {
        comm_error = std::make_shared<Comm_error>(CE_FAILURE, "Socket_API::receive", "Cannot build the message header: " + std::string{ ec.what() });
        return false; // it cannot do any other action here
    }

    if(expectedMessage != MSG_UNDEFINED && message->code != expectedMessage && message->code != MSG_ERROR) {
//        message = Message::error( new Comm_error{ CE_UNEXPECTED_TYPE, "Socket_API::receive_header_", "Expected message code " + std::to_string(expectedMessage) + " but actual code is " + std::to_string(message->code) });
//        comm_error = message->comm_error;
        comm_error = std::make_shared<Comm_error>(CE_UNEXPECTED_TYPE, "Socket_API::receive_header_", "Expected message code " + std::to_string(expectedMessage) + " but actual code is " + std::to_string(message->code));
        status = false;
    }

    if(!receive_content_() || !status) // read the message content in any case
        return false; // it cannot do any other action here
    try {
        auto new_message = message->build_content(); // build the whole message
        message = new_message;
    } catch(const std::exception &ec) {
        comm_error = std::make_shared<Comm_error>(CE_FAILURE, "Socket_API::receive", "Cannot build the message content: " + std::string{ ec.what() });
        return false; // it cannot do any other action here
    }
    comm_error = message->comm_error;

    Logger::info("Socket_API::receive", "Receiving a message... - done", PR_LOW);

    return status;
}

bool Socket_API::shutdown() {
    boost::system::error_code ec;
    try {
        if(socket_ && socket_->is_open())
            socket_->shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
    } catch(const boost::system::error_code &e) {
        comm_error = std::make_shared<Comm_error>(CE_FAILURE, "Socket_API::close_conn", e.message(), e);
        return false;
    }
    return true;
}

bool Socket_API::close_conn(bool force) {
    boost::system::error_code ec;

    Logger::info("Socket_API::close_conn", "Closing a connection...", PR_VERY_LOW);

    if(socket_ == nullptr)
        return true;
    if(socket_->is_open() && keep_alive && !force)
        return true;
    Logger::info("Socket_API::close_conn", "Force closing", PR_VERY_LOW);

    if(!shutdown())
        return false;

    try {
        if(socket_->is_open()) {
//            std::cerr << "1 " + std::to_string(socket_->is_open()) << std::endl;
            socket_->release(ec);
//            std::cerr << "2 " + std::to_string(socket_->is_open()) << std::endl;
            socket_->close(ec);
//            std::cerr << "3 " + std::to_string(socket_->is_open()) << std::endl;
            socket_ = nullptr;
        }
    } catch(const boost::system::error_code &e) {
        comm_error = std::make_shared<Comm_error>(CE_FAILURE, "Socket_API::close_conn", e.message(), e);
        return false;
    }

    if(ec)
        comm_error = std::make_shared<Comm_error>(CE_FAILURE, "Socket_API::close_conn", ec.message(), ec);

    socket_.reset(nullptr); // delete
    Logger::info("Socket_API::close_conn", "Closing a connection... - done", PR_VERY_LOW);
    return true;
}

std::shared_ptr<Message> Socket_API::get_message() const {
    return message;
}
std::shared_ptr<Comm_error> Socket_API::get_last_error() const {
    return comm_error ? comm_error : message->comm_error;
}

Socket_API::~Socket_API() {
    boost::system::error_code ec;
    if(socket_ != nullptr && socket_->is_open()) {
        socket_->shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
        socket_->release(ec);
        socket_->close(ec);
        ctx_.stop();
    }
    socket_.reset(nullptr);
}

