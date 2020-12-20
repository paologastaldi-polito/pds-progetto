//
// Created by stevezbiz on 26/11/20.
//

#include "Socket_API.h"

#include <utility>

void Socket_API::init_config_() {
    boost::asio::ip::tcp::resolver resolver(this->ctx_);
    Logger::info("Socket_API::init_config_", "Resolving to " + this->ip + ":" + this->port, PR_VERY_LOW);
    this->endpoint_iterator_ = resolver.resolve({ this->ip, this->port });
}

bool Socket_API::call_(const std::function<void(boost::asio::ip::tcp::socket &, boost::system::error_code &)> &perform_this) {
    bool status = false;
    bool stop = false;
    int retry_cont = 0;
    boost::system::error_code ec;

    try {
        while (!stop && retry_cont <= this->n_retry_) {
            if(retry_cont > 0)
                Logger::warning("Socket_API::call_", "Retry " + std::to_string(retry_cont));
            if(!(this->socket_ && this->socket_->is_open())) {
                Logger::warning("Socket_API::call_", "Invalid socket");
                this->comm_error = std::make_shared<Comm_error>(CE_FAILURE, "Socket_API::call_", "Invalid socket");
                break;
            }

            perform_this(*this->socket_, ec);

            if(!ec.failed()) {
                stop = true;
                status = true;
                ec.clear();
            } else {
                this->comm_error = std::make_shared<Comm_error>(CE_FAILURE, "Socket_API::call_", "Operation failure on socket", ec);
                std::this_thread::sleep_for(std::chrono::milliseconds(retry_delay_));
                retry_cont++;
            }
        }
    } catch(const std::exception &ec) {
        this->comm_error = std::make_shared<Comm_error>(CE_GENERIC, "Socket_API::call_", "Exception caught: " + std::string{ ec.what() }); // never used
        return false;
    }

    return status;
}

bool Socket_API::generic_handler_(const boost::system::error_code &ec, std::size_t bytes_transferred) {
    std::cout << ec << " errors, " <<bytes_transferred << " bytes as been transferred correctly" << std::endl;
    return ec.value();
}

bool Socket_API::receive_header_() {
    if(!this->call_([this](boost::asio::ip::tcp::socket &socket, boost::system::error_code &ec) {
            boost::asio::read(socket, this->message->get_header_buffer(), ec);
        })) {
        this->comm_error = std::make_shared<Comm_error>(CE_FAILURE, "Socket_API::receive_header_", "Unable to read message header");
        return false;
    }

    return true;
}

bool Socket_API::receive_content_() {
    if(!this->call_([this](boost::asio::ip::tcp::socket &socket, boost::system::error_code &ec) {
            boost::asio::read(socket, this->message->get_content_buffer(), ec);
        })) {
        this->comm_error = std::make_shared<Comm_error>(CE_FAILURE, "Socket_API::receive_content_", "Unable to read message content");
        return false;
    }

    return true;
}

Socket_API::Socket_API(std::string ip, std::string port, ERROR_MANAGEMENT error_management, long retry_delay, bool keep_alive) :
        ip(std::move(ip)),
        port(std::move(port)),
        n_retry_(error_management),
        retry_delay_(retry_delay),
        keep_alive_(keep_alive) {
    this->init_config_();
}

Socket_API::Socket_API(boost::asio::ip::tcp::socket socket, ERROR_MANAGEMENT error_management, long retry_delay, bool keep_alive) :
        n_retry_(error_management),
        retry_delay_(retry_delay),
        keep_alive_(keep_alive) {
    this->socket_ = std::make_unique<boost::asio::ip::tcp::socket>(std::move(socket));
}

bool Socket_API::open_conn(bool force) {
    Logger::info("Socket_API::open_conn", "Opening a connection...", PR_VERY_LOW);

    if(this->socket_ != nullptr && this->socket_->is_open() && this->keep_alive_ && !force) {
        Logger::info("Socket_API::open_conn", "Connection already opened", PR_VERY_LOW);
        return true;
    }

    if(!this->close_conn() || this->ctx_.stopped()) // close current socket in a safe way
        return false;

    auto f = std::async(std::launch::async, [this]() {
        try {
            Logger::info("Socket_API::open_conn", "Creating the socket...", PR_VERY_LOW);
            Logger::info("Socket_API::open_conn", "Opening socket", PR_VERY_LOW);
//                auto socket = new boost::asio::ip::tcp::socket{ this->ctx_ };
            boost::asio::ip::tcp::socket socket{ this->ctx_ };
            Logger::info("Socket_API::open_conn", "Connecting", PR_VERY_LOW);
            boost::asio::connect(socket, this->endpoint_iterator_);
            Logger::info("Socket_API::open_conn", "Moving the socket", PR_VERY_LOW);
//                this->socket_ = std::unique_ptr<boost::asio::ip::tcp::socket>(socket);
            this->socket_ = std::make_unique<boost::asio::ip::tcp::socket>(std::move(socket));
            Logger::info("Socket_API::open_conn", "Everything is done", PR_VERY_LOW);
        } catch(const std::exception &e) {
            this->comm_error = std::make_shared<Comm_error>(CE_FAILURE, "Socket_API::open_conn", e.what());
            return false;
        }
        return true;
    });
    auto future_status = f.wait_for(std::chrono::milliseconds(CONN_TIMEOUT));
    if(future_status == std::future_status::timeout) {
        this->comm_error = std::make_shared<Comm_error>(CE_FAILURE, "Socket_API::open_conn", "Unable to connect, timeout");
        return false;
    }
    Logger::info("Socket_API::open_conn", "Opening a connection... - done", PR_VERY_LOW);

    return f.get();
}

bool Socket_API::send(std::shared_ptr<Message> message) {
    Logger::info("Socket_API::send", "Sending a message...", PR_LOW);

    message->keep_alive = this->keep_alive_;
    if(!this->call_([&message](boost::asio::ip::tcp::socket &socket, boost::system::error_code &ec) {
            boost::asio::write(socket, message->send(), ec);
        })) {
        this->comm_error = std::make_shared<Comm_error>(CE_FAILURE, "Socket_API::send", "Unable to write message");
        return false;
    }
    Logger::info("Socket_API::send", "Sending a message... - done", PR_LOW);

    return true;
}

bool Socket_API::receive(MESSAGE_TYPE expectedMessage) {
    bool status = true;
    this->message = std::make_shared<Message>();
    Logger::info("Socket_API::receive", "Receiving a message...", PR_LOW);

    if(!this->receive_header_())
        return false; // it cannot do any other action here

    try {
        this->message = this->message->build_header(); // build the header
    } catch(const std::exception &ec) {
        this->comm_error = std::make_shared<Comm_error>(CE_FAILURE, "Socket_API::receive", "Cannot build the message header: " + std::string{ ec.what() });
        return false; // it cannot do any other action here
    }

    if(expectedMessage != MSG_UNDEFINED && message->code != expectedMessage && message->code != MSG_ERROR) {
        this->message = Message::error(new Comm_error{ CE_UNEXPECTED_TYPE, "Socket_API::receive_header_", "Expected message code " + std::to_string(expectedMessage) + " but actual code is " + std::to_string(message->code) });
        this->comm_error = this->message->comm_error;
        status = false;
    }

    if(!this->receive_content_()) // read the message content in any case
        return false; // it cannot do any other action here

    try {
        auto new_message = message->build_content(); // build the whole message
        this->message = new_message;
    } catch(const std::exception &ec) {
        this->comm_error = std::make_shared<Comm_error>(CE_FAILURE, "Socket_API::receive", "Cannot build the message content: " + std::string{ ec.what() });
        return false; // it cannot do any other action here
    }
    this->comm_error = this->message->comm_error;
    Logger::info("Socket_API::receive", "Receiving a message... - done", PR_LOW);

    return status;
}

bool Socket_API::shutdown() {
    boost::system::error_code ec;
    try {
        if(this->socket_ && this->socket_->is_open())
            this->socket_->shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
    } catch(const boost::system::error_code &e) {
        this->comm_error = std::make_shared<Comm_error>(CE_FAILURE, "Socket_API::close_conn", e.message(), e);
        return false;
    }
    return true;
}

bool Socket_API::close_conn(bool force) {
    boost::system::error_code ec;

    Logger::info("Socket_API::close_conn", "Closing a connection...", PR_VERY_LOW);

    if(this->socket_ == nullptr)
        return true;
    if(this->socket_->is_open() && this->keep_alive_ && !force)
        return true;
    Logger::info("Socket_API::close_conn", "Force closing", PR_VERY_LOW);

    if(!this->shutdown())
        return false;

    try {
        if(this->socket_->is_open()) {
//            std::cerr << "1 " + std::to_string(this->socket_->is_open()) << std::endl;
            this->socket_->release(ec);
//            std::cerr << "2 " + std::to_string(this->socket_->is_open()) << std::endl;
            this->socket_->close(ec);
//            std::cerr << "3 " + std::to_string(this->socket_->is_open()) << std::endl;
            this->socket_ = nullptr;
        }
    } catch(const boost::system::error_code &e) {
        this->comm_error = std::make_shared<Comm_error>(CE_FAILURE, "Socket_API::close_conn", e.message(), e);
        return false;
    }

    if(ec)
        this->comm_error = std::make_shared<Comm_error>(CE_FAILURE, "Socket_API::close_conn", ec.message(), ec);

    this->socket_.reset(nullptr); // delete
    Logger::info("Socket_API::close_conn", "Closing a connection... - done", PR_VERY_LOW);
    return true;
}

std::shared_ptr<Message> Socket_API::get_message() const {
    return this->message;
}
std::shared_ptr<Comm_error> Socket_API::get_last_error() const {
    return this->comm_error ? this->comm_error : this->message->comm_error;
}

Socket_API::~Socket_API() {
    boost::system::error_code ec;
    if(this->socket_ != nullptr && this->socket_->is_open()) {
        this->socket_->shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
        this->socket_->release(ec);
        this->socket_->close(ec);
    }
    this->socket_.reset(nullptr);
}

