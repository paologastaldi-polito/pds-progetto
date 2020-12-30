//
// Created by Paolo Gastaldi on 07/12/2020.
//

#include "Session_manager.h"

std::shared_ptr<Session> Session_manager::retrieve_session(const std::shared_ptr<Message> &message) {
    std::shared_ptr<Session> session{ nullptr };
    auto plain_cookie = Utils::verify_cookie(message->cookie);
    std::locale loc;
    if(std::isdigit(plain_cookie[0], loc)) {
        Logger::info("Session_manager::retrieve_session", "cookie: " + plain_cookie, PR_VERY_LOW);
        auto it = this->sessions_.find(std::stoi(plain_cookie));
        if (it != this->sessions_.end()) // if already exists
            session = it->second;
    }
    else {
        int new_session_id = this->session_counter_++;
        auto new_session = std::make_shared<Session>(new_session_id);
        this->sessions_.insert_or_assign(new_session_id, new_session);
        session = new_session;
    }
    session->thread_count++;
    return session;
}

void Session_manager::pause_session(const std::shared_ptr<Message> &message) {
    auto session = this->retrieve_session(message);
    if(session->thread_count) // weak check
        session->thread_count--;
}

bool Session_manager::remove_session(const std::shared_ptr<Session> &session) {
    auto it = this->sessions_.find(session->session_id);
    if(it == this->sessions_.end())
        return false;

    it->second.reset();
    this->sessions_.erase(session->session_id);
    return true;
}

Session_manager::~Session_manager() {
    this->sessions_.clear();
}

std::shared_ptr<Session> Session_manager::get_empty_session() {
    return std::make_shared<Session>(-1);
}
