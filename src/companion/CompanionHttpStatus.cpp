#include "companion/CompanionHttp.h"

#include <WebServer.h>

namespace companion::api {

    std::string httpStatusLine(t_http_codes status) {
        const int code = static_cast<int>(status);
        const String reason = WebServer::responseCodeToString(code);

        std::string line = std::to_string(code);
        line.push_back(' ');
        line.append(reason.c_str(), reason.length());
        return line;
    }

} // namespace companion::api
