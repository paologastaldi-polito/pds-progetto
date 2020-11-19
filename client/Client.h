//
// Created by stevezbiz on 07/11/20.
//

#ifndef CLIENT_CLIENT_H
#define CLIENT_CLIENT_H

#include <boost/filesystem/path.hpp>
#include <boost/log/trivial.hpp>
#include <boost/asio.hpp>
#include <iostream>
#include <deque>
#include <array>
#include <fstream>
#include "Message.h"

class Client {
    boost::asio::io_context &ctx_;
    boost::asio::ip::tcp::socket socket_;
    Message read_msg_;
    std::deque<Message> write_msgs_;

    enum { MessageSize = 1024 };
    std::array<char, MessageSize> m_buf;
    boost::asio::streambuf m_request;
    std::ifstream m_sourceFile;
    std::string m_path;

    void do_connect(boost::asio::ip::tcp::resolver::iterator endpoint_iterator);

    void do_read_header();

    void do_read_body();

    void do_write();

    void open_file(std::string const& t_path);

    void do_write_file(const boost::system::error_code& t_ec);

    template<class Buffer>
    void write_buffer(Buffer& t_buffer);

public:
    Client(boost::asio::io_context &ctx, boost::asio::ip::tcp::resolver::iterator endpoint_iterator);

    void write(const Message &msg);

    void close();
};




#endif //CLIENT_CLIENT_H
