#include "http_client.h"

#include <arpa/inet.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/tcp.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <functional>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>

#include "http_message.h"
#include "uri.h"

#include <iostream>
#include <fstream>

namespace simple_http_client {

    HttpClient::HttpClient(const std::string &host, std::uint16_t port)
    : host_(host),
      port_(port),
      sock_fd_(0),
      connected_(false),
      running_(false),
      worker_epoll_fd_(),
      rng_(std::chrono::steady_clock::now().time_since_epoch().count()),
      sleep_times_(10, 100) {
    CreateSocket();    
    }

    bool HttpClient::Connect() {
        if (sock_fd_ == 0) {
            CreateSocket();
        }

        int opt = 1;
        // Устанавливаем опции сокса
        if (setsockopt(sock_fd_, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)) < 0) {
            throw std::runtime_error("Failed to set socket options");
            return false;
        }

        // Создаем структуру sockaddr_in для сервера
        struct sockaddr_in server_address;
        server_address.sin_family = AF_INET;
        if (inet_pton(AF_INET, host_.c_str(), &(server_address.sin_addr)) < 0) {
            throw std::runtime_error("Error: Invalid address or address not supported");
            return false;
        }
        server_address.sin_port = htons(port_);

        // Устанавливаем соединение с сервером
        if ((connect(sock_fd_, (struct sockaddr *)&server_address, sizeof(server_address))) == 0) {
            throw std::runtime_error("Failed to connect to the server");
            return false;
        }

        // Инициализируем необходимые ресурсы для клиента
        InitializeClientResources();

        // Запускаем поток для приема данных от сервера
        receive_thread_ = std::thread(&HttpClient::ReceiveData, this);
        running_ = true;
        connected_ = true;

        return true;
    }

    void HttpClient::Disconnect() {
        running_ = false;
        if (receive_thread_.joinable()) {
            receive_thread_.join();
        }
        Close();
    }

    void HttpClient::ReceiveFile()
    {
        char file_size_str[16];
        char file_name[32];
        
        recv(sock_fd_, file_size_str, 16, 0);
        int file_size = atoi(file_size_str);
        char* bytes = new char[file_size];

        recv(sock_fd_, file_name, 32, 0);

        std::fstream file;
        file.open(file_name, std::ios_base::out | std::ios_base:: binary);

        if (file.is_open())
        {
            recv(sock_fd_,bytes, file_size, 0);
            file.write(bytes, file_size);
        } else {
            throw std::runtime_error("Error file open");
        }
        
        delete[] bytes;
        file.close();

    }

    bool HttpClient::SendHttpRequest(HttpRequest& request)
    {
        if (!connected_)
        {
            std::cerr << "Error: Not connected to the server" << std::endl;
            return false;
        }
        HttpMethod method = request.method();
        switch (method)
        {
        case simple_http_client::HttpMethod::DOWNLOAD:
        {
            // выбрать метод HTTP
            std::string method_string = "DOWNLOAD";
            HttpMethod method = string_to_method(method_string);
            request.SetMethod(method);

            // выбрать путь (URI) и тело запроса
            std::string path = "/";
            request.SetUri(simple_http_client::Uri(path));

            std::cout << "Введите тело запроса HTTP/1.1 (ссылка): ";
            std::string body = "https://www.stats.govt.nz/assets/Uploads/Annual-enterprise-survey/Annual-enterprise-survey-2021-financial-year-provisional/Download-data/annual-enterprise-survey-2021-financial-year-provisional-csv.csv";
            request.SetContent(body);

            // Преобразуем объект HttpRequest в строку и отправляем на сервер
            std::string request_string = to_string(request);

            // Отправляем HTTP-запрос
            ssize_t sent_bytes = send(sock_fd_, request_string.c_str(), request_string.length(), 0);
            if (sent_bytes == -1) {
                std::cerr << "Error: Failed to send the HTTP request" << std::endl;
                return false;
            }
            return true;
            break;
        }
        case simple_http_client::HttpMethod::SAVEAS:
        {

        }
        default:
            return false;
            break;
        }
    };
    void HttpClient::ReceiveHttpResponse(const std::string& response_string) {
        // Преобразование строки ответа в объект HttpResponse
        HttpResponse parsed_response = string_to_response(response_string);

        if (parsed_response.status_code() == HttpStatusCode::Ok) {
            std::string content = parsed_response.content();

            std::string filename = "annual-enterprise-survey-2021-financial-year-provisional-csv.csv";
            std::ofstream outfile("Download/" + filename);

            if (outfile.is_open()) {
                outfile << content;
                outfile.close();
                std::cout << "File saved successfully." << std::endl;
                // Можно здесь вызвать обработчик для сигнала об успешном сохранении файла
            } else {
                std::cerr << "Error: Failed to save the file." << std::endl;
                // Можно здесь вызвать обработчик для сигнала об ошибке сохранения файла
            }
        } else {
            std::cerr << "Error: Server responded with an error: " << static_cast<int>(parsed_response.status_code()) << std::endl;
            // Можно здесь вызвать обработчик для сигнала об ошибке в ответе сервера
        }
    }



    void HttpClient::CreateSocket() {
        if ((sock_fd_ = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0)) < 0) {
            throw std::runtime_error("Failed to create a TCP socket");
        }
    }

    void HttpClient::InitializeClientResources() {
        // Настраиваем размер буфера приема данных (receive buffer size)
        int receive_buffer_size = 1024;
        if (setsockopt(sock_fd_, SOL_SOCKET, SO_RCVBUF, &receive_buffer_size, sizeof(int)) < 0) {
            throw std::runtime_error("Failed to set receive buffer size");
        }

        // TCP_NODELAY, чтобы отключить алгоритм накопления пакетов
        int tcp_nodelay = 1;
        if (setsockopt(sock_fd_, IPPROTO_TCP, TCP_NODELAY, &tcp_nodelay, sizeof(int)) < 0) {
            throw std::runtime_error("Failed to set TCP_NODELAY");
        }

        // Место для других настроек и опций сокса
    }

    void HttpClient::ReceiveData() {
        char buffer[kMaxBufferSize];
        ssize_t received_bytes;

        while (running_) {
            received_bytes = recv(sock_fd_, buffer, kMaxBufferSize, 0);
            if (received_bytes > 0) {
                // Обработка принятых данных, например, добавление их в очередь
                data_mutex_.lock();
                data_queue_.push(std::string(buffer, received_bytes));
                data_mutex_.unlock();
            } else if (received_bytes == 0) {
                // Соединение было закрыто сервером
                running_ = false;
            } else {
                // Ошибка при приеме данных
                // Можно добавить логирование или другую обработку ошибки
            }
        }
    }

    void HttpClient::ResponseData()
    {
        if (data_mutex_.try_lock())
        {
            if (data_queue_.size() > 0)
            {
                data_response_ = data_queue_.back();
                data_queue_.pop();
                std::cout << data_response_ << std::endl;
            }
            data_mutex_.unlock();
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    // void HttpClient::ResponseData() {
    //     if (data_mutex_.try_lock()) {
    //         if (data_queue_.size() > 0) {
    //             std::string response_string = data_queue_.back(); // Получаем ответ из очереди
    //             data_queue_.pop();
    //             std::cout << response_string << std::endl;

    //             // Обработка ответа
    //             ReceiveHttpResponse(response_string);
    //         }
    //         data_mutex_.unlock();
    //     }
    //     std::this_thread::sleep_for(std::chrono::seconds(1));
    // }

    void HttpClient::Close() {
        if (connected_) {
            close(sock_fd_);
            connected_ = false;
        }
    }

} // namespace simple_http_client
