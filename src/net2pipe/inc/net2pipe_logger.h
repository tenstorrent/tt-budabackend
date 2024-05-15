// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#ifndef _NET2PIPE_LOGGER_H_
#define _NET2PIPE_LOGGER_H_

#include <sstream>
#include <iostream>
#include <string>
#include <algorithm>

namespace n2p{
enum Verb {
    None,
    Info,
    Full
};

class cLog {
    public:
        cLog(){ 
            get_level();
            v = Info;
        }
        ~cLog(){}

        template<typename T>
        cLog& operator<<(T const&val)
        {
            if (level >= v) {
                std::cout << val;
            }
            return *this;
        }

        cLog& operator<<(std::ostream&(*f)(std::ostream&))
        {
            if (level >= v) {
                std::ostringstream s;
                s << f;
                std::cout << s.str();
            }
            return *this;
        }

        cLog& operator()(Verb _v) { 
            v = _v;
            return *this;
        }

        cLog& operator()() { 
            v = Info;
            return *this;
        }

    private:
        Verb v;
        Verb level;

        void get_level() {
            level = None;

            static char const* level_env = std::getenv("LOGGER_LEVEL");
            if (level_env) {
                std::string level_str = level_env;
                std::transform(
                    level_str.begin(), level_str.end(), level_str.begin(), [](unsigned char c) { return std::toupper(c); });
                if (level_str.compare("TRACE") == 0) {
                    level = Full;
                } else if (level_str.compare("DEBUG") == 0) {
                    level = Info;
                }
            }
        }

};

extern cLog Log;

}

#endif