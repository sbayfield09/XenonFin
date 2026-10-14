#pragma once
#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace xf {
struct Error : std::runtime_error {
    int status;
    explicit Error(const std::string& message, int code = 0) : std::runtime_error(message), status(code) {}
};
struct Url {
    std::string base, origin;
    static Url parse(std::string input);
    std::string resolve(const std::string& relative) const;
};
std::string encode(const std::string& input);
using Headers = std::vector<std::string>;
using Cancel = std::shared_ptr<std::atomic<bool>>;
struct Response {
    long status = 0;
    std::string body;
    std::map<std::string, std::string> headers;
};
struct Request {
    std::string method = "GET", url, body;
    Headers headers;
    Cancel cancel;
    size_t max_bytes = 4 * 1024 * 1024;
    long timeout_seconds = 20;
};
struct Transport {
    virtual ~Transport() = default;
    virtual Response request(const Request&) = 0;
};
class Http final : public Transport {
    void* handle_=nullptr;
    std::mutex mutex_;
public:
    Http();
    ~Http() override;
    Response request(const Request&) override;
};
void check_response(const Response&);
}
