//
// Created by stevezbiz on 01/11/2020.
//

#include "FileWatcher.h"
#include <thread>
#include <boost/filesystem.hpp>

namespace fs = boost::filesystem;

FileWatcher::FileWatcher(std::string path_to_watch, std::chrono::duration<int, std::milli> delay) :
        path_to_watch_(std::move(path_to_watch)), delay_(delay), running_(true) {
    for (const auto &el : fs::recursive_directory_iterator(path_to_watch_)) {
        if (fs::is_directory(el.path())) {
            FSElement dir{el.path(), ElementType::dir, fs::last_write_time(el.path())};
            paths_.insert(std::make_pair(el.path().string(), dir));
        } else {
            FSElement file{el.path(), ElementType::file, fs::last_write_time(el.path())};
            paths_.insert(std::make_pair(el.path().string(), file));
        }

    }
}

void FileWatcher::start(const std::function<void(std::string, ElementStatus)> &action) {
    while (running_) {
        std::this_thread::sleep_for(delay_);
        // controlla se un elemento è stato cancellato
        findErased(action);
        // cntrolla se un elemento è stato creato o modificato
        findCreatedOrModified(action);
    }
}

bool FileWatcher::contains(const std::string &key) {
    auto el = paths_.find(key);
    return el != paths_.end();
}

void FileWatcher::findErased(const std::function<void(std::string, ElementStatus)> &action) {
    auto it = paths_.begin();
    while (it != paths_.end()) {
        if (!fs::exists(it->first)) {
            if (it->second.getType() == ElementType::dir) {
                action(it->first, ElementStatus::erasedDir);
            } else {
                action(it->first, ElementStatus::erasedFile);
            }
            it = paths_.erase(it);
        } else {
            it++;
        }
    }
}

void FileWatcher::findCreatedOrModified(const std::function<void(std::string, ElementStatus)> &action) {
    for (const auto &el : fs::recursive_directory_iterator(path_to_watch_)) {
        time_t lwt = fs::last_write_time(el);
        if (!contains(el.path().string())) {
            if (fs::is_directory(el)) {
                paths_.insert(std::make_pair(el.path().string(), FSElement{el.path(), ElementType::dir, lwt}));
                action(el.path().string(), ElementStatus::createdDir);
            } else {
                paths_.insert(std::make_pair(el.path().string(), FSElement{el.path(), ElementType::file, lwt}));
                action(el.path().string(), ElementStatus::createdFile);
            }
        } else if (fs::is_regular_file(el)) {
            if (paths_.find(el.path().string())->second.isOld(lwt)) {
                if (paths_.find(el.path().string())->second.needUpdate()) {
                    action(el.path().string(), ElementStatus::modifiedFile);
                }
            }
        }
    }
}
