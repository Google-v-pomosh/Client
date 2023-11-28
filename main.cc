#include "http_client.h"
#include "http_message.h"
#include "uri.h"

#include <iostream>
#include <string>
#include <thread>
#include <chrono>

int main() {
    // Указываем адрес и порт сервера
    std::string host = "0.0.0.0";
    int port = 8080;

    // Создаем экземпляр HttpClient
    simple_http_client::HttpClient client(host, port);
    simple_http_client::HttpRequest request;

    // Устанавливаем соединение с сервером
    if (!client.Connect()) {
        std::cerr << "Ошибка при установлении соединения с сервером" << std::endl;
        return -1;
    }

    request.SetMethod(simple_http_client::HttpMethod::DOWNLOAD);

    if (!client.SendHttpRequest(request)) {
        std::cerr << "Ошибка при отправке HTTP-запроса" << std::endl;
        return -1;
    }

    client.ReceiveFile();

    // Получаем и выводим ответ от сервера
    while (true)
    {
        client.ResponseData();
        std::cout << "TEST" << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    client.Disconnect();

    return 0;
}
