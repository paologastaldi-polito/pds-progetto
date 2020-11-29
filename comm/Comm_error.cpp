//
// Created by Paolo Gastaldi on 26/11/2020.
//

#include "Comm_error.h"

#include <utility>

template<class Archive>
void Comm_error::serialize(Archive &ar, const unsigned int version) {
    ar & this->comm_errno;
    ar & this->message;
    ar & this->location;
}

Comm_error::Comm_error(COMM_ERRNO comm_errno, std::string location, std::string message) : comm_errno(comm_errno), location(std::move(location)), message(std::move(message)) {}

std::string Comm_error::send() const {
    std::ostringstream ostream{};
    boost::archive::text_oarchive oa{ ostream };

    // serialize message content
    oa << this;
    return ostream.str();
}
