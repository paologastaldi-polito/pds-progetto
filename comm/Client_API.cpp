//
// Created by stevezbiz on 26/11/20.
//

#include <set>
#include <utility>
#include "Client_API.h"

bool Client_API::get_and_save_(const std::string &path) {
    if(!this->api_->send_and_receive(Message::get(path), MSG_GET_CONTENT))
        return false;
    auto res = this->api_->get_message();
    if (!res->is_okay())
        return false;
    boost::filesystem::path dest_path{root_path_};
    dest_path.append(path);
    res->path = dest_path.string();
    if(res->elementStatus==ElementStatus::createdFile){
        if (!Client_API::save_file_(res))
            return false;
    } else {
        boost::filesystem::create_directory(dest_path);
    }

    return true;
}

Client_API::Client_API(Client_socket_API *socket_api, std::string root_path) : API(socket_api), api_(socket_api), root_path_(std::move(root_path)) {}

bool Client_API::login(const std::string &username, const std::string &password) {
    Logger::info("Client_API::login", "Trying to perform login...", PR_LOW);
    if(!this->api_->send_and_receive(Message::login(username, password), MSG_OKAY))
        return false;
    auto res = this->api_->get_message();
    Logger::info("Client_API::login", "Trying to perform login... - done", PR_LOW);

    return res->is_okay();
}

bool Client_API::probe(const std::map<std::string, std::string> &map) {
    Logger::info("Client_API::probe", "Probe check started...", PR_LOW);

    std::vector<std::string> paths;
    paths.reserve(map.size());
    for (auto const &item : map)
        paths.push_back(item.first);
    if(!this->api_->send_and_receive(Message::probe(paths), MSG_PROBE_CONTENT))
        return false;
    auto res = this->api_->get_message();

    if(!res->is_okay())
        return false;

    // if the client has a different file than the server, the client starts a push procedure
    for(auto const &item : map) {
        auto path = item.first;
        Logger::info("Client_API::probe", "Analyzing " + path + "...", PR_VERY_LOW);
        auto it = res->hashes.find(path);
        boost::filesystem::path dest_path{root_path_};
//        dest_path.append(path);
        dest_path /= path ;
        if(boost::filesystem::is_regular_file(dest_path)){
            if(it==res->hashes.end()){
                // file not registered -> create
                Logger::info("Client_API::probe", "Created file found " + path, PR_LOW);
                if (!this->push(Utils::read_from_file(dest_path), path, map.at(path), ElementStatus::createdFile))
                    return false;
            } else if(res->hashes.at(path)!=item.second){
                // different hash -> modified
                Logger::info("Client_API::probe", "Modified file found " + path, PR_LOW);
                if (!this->push(Utils::read_from_file(dest_path), path, map.at(path),ElementStatus::modifiedFile))
                    return false;
            }
        } else if(boost::filesystem::is_directory(dest_path)){
            if(it==res->hashes.end()){
                // dir not registered -> create
                Logger::info("Client_API::probe", "Created dir found " + path, PR_LOW);
                if (!this->push(std::vector<unsigned char>(), path, map.at(path), ElementStatus::createdDir))
                    return false;
            }
        }
    }

    for(auto const &item : res->hashes) {
        auto path = item.first;
        Logger::info("Client_API::probe", "Analyzing " + path + " from server...", PR_VERY_LOW);
        boost::filesystem::path dest_path{root_path_};
        dest_path /= path ;
        auto it = map.find(path);

        auto hash = item.second;
        if(it == map.end()) {
            if(hash != "") { // file
                // file not found -> erase
                Logger::info("Client_API::probe", "Deleted file found " + path, PR_LOW);
                if (!this->push(std::vector<unsigned char>(), path, "", ElementStatus::erasedFile))
                    return false;
            }
            else { // dir
                // dir not found -> erase
                Logger::info("Client_API::probe", "Deleted dir found " + path, PR_LOW);
                if (!this->push(std::vector<unsigned char>(), path, "", ElementStatus::erasedDir))
                    return false;
            }
        }
    }
    Logger::info("Client_API::probe", "Probe check started... - done", PR_LOW);

    return true;
}

bool Client_API::push(const std::vector<unsigned char> &file, const std::string &path, const std::string &hash, ElementStatus elementStatus) {
    Logger::info("Client_API::push", "Push started...");
    if(!this->api_->send_and_receive(Message::push(file, path, hash, elementStatus),MSG_OKAY))
        return false;
    auto res = this->api_->get_message();
    Logger::info("Client_API::push", "Push started... - done");

    return res->is_okay();
}

bool Client_API::restore() {
    boost::filesystem::remove_all(root_path_);
    boost::filesystem::create_directory(root_path_);
    if(!this->api_->send_and_receive(Message::restore(),MSG_RESTORE_CONTENT))
        return false;
    auto res = this->api_->get_message();

    if(!res->is_okay())
        return false;

    std::set<std::string> paths;
    for(const auto& el : res->paths){
        paths.insert(el);
    }
    for(const auto &path : paths){
        if (!this->get_and_save_(path))
            return false;
    }

    return true;
}

bool Client_API::end() {
    Logger::info("Client_API::end", "End started...", PR_LOW);
    if(!this->api_->send_and_receive(Message::end(), MSG_OKAY))
        return false;
    auto res = this->api_->get_message();
    Logger::info("Client_API::end", "End started... - done", PR_LOW);
    return res->is_okay();
}
