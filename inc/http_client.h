#ifndef HTTP_CLIENT_H_
#define HTTP_CLIENT_H_

#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <chrono>
#include <functional>
#include <map>
#include <random>
#include <string>
#include <thread>
#include <mutex>
#include <queue>
#include <utility>

#include "http_message.h"
#include "uri.h"

namespace simple_http_client {

class HttpClient
{
    public:
        explicit HttpClient(const std::string& host, std::uint16_t port);
        HttpClient() = default;

        HttpClient(HttpClient&&) = default;
        HttpClient& operator = (HttpClient&&) = default;

        ~HttpClient() = default;

        bool Connect();
        void Disconnect();
        void ReceiveFile();

        bool SendHttpRequest(HttpRequest& request);
        void ReceiveHttpResponse(const std::string& response_string);
        void ResponseData();

    private:
        static constexpr size_t kMaxBufferSize = 4096;
        static constexpr int kThreadPoolSize = 5;

        std::string host_;
        std::uint16_t port_;
        int sock_fd_;
        bool running_;
        bool connected_;
        int worker_epoll_fd_[kThreadPoolSize];
        std::mt19937 rng_;
        std::uniform_int_distribution<int> sleep_times_;
        std::thread receive_thread_;
        std::mutex data_mutex_;
        std::queue<std::string> data_queue_;
        std::string data_response_;

        void CreateSocket();
        void InitializeClientResources();
        void Close();
        void ReceiveData();
};

}  // namespace simple_http_client

#endif  // HTTP_CLIENT_H_
