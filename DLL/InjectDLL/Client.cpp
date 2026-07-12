#pragma once

#pragma comment(lib, "ws2_32")
#include "Connectivity.h"
#include <string>

using namespace Connectivity;

bool Client::connectToServer(std::string IP, std::string PORT)
{
    if (WSAStartup(MAKEWORD(2, 0), &WSAData) != 0)
        return false;

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    addrinfo* addresses = nullptr;
    if (getaddrinfo(IP.c_str(), PORT.c_str(), &hints, &addresses) != 0)
        return false;

    server = INVALID_SOCKET;
    for (addrinfo* address = addresses; address != nullptr; address = address->ai_next)
    {
        SOCKET candidate = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (candidate == INVALID_SOCKET)
            continue;
        if (::connect(candidate, address->ai_addr, static_cast<int>(address->ai_addrlen)) == 0)
        {
			int noDelay = 1;
			setsockopt(candidate, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));
            server = candidate;
            break;
        }
        closesocket(candidate);
    }
    freeaddrinfo(addresses);
    return server != INVALID_SOCKET;
}

void Client::sendMessage(std::string command, std::string message)
{
    int dataSize = message.size();

    std::string messageToSend = std::to_string(dataSize);

    int numberOfZeros = 5 - messageToSend.size();

    for (int i = 0; i < numberOfZeros; i++)
    {
        messageToSend = "0" + messageToSend;
    }

    messageToSend += command;

    numberOfZeros = 11 - command.size();

    for (int i = 0; i < numberOfZeros; i++)
    {
        messageToSend += "0";
    }

    messageToSend += ";" + message + "END";

    for (int i = 0; i < messageToSend.size(); i++)
    {
        this->buffer[i] = messageToSend[i];
    }

    sendAll(buffer, sizeof(buffer));
    memset(buffer, 0, sizeof(buffer));
}

std::string Client::receive()
{

    std::string appendable = "";
    bool CompleteMessage = false;

    while (!CompleteMessage)
    {
        int received = recv(server, buffer, sizeof(buffer), 0);
        if (received <= 0)
            return "";
        appendable.append(buffer, static_cast<std::size_t>(received));
        memset(buffer, 0, sizeof(buffer));

        rapidjson::Document document;
        document.Parse(appendable.c_str(), appendable.size());
        CompleteMessage = !document.HasParseError();
    }

    return appendable;
}

bool Client::sendBytes(byte Message[7168])
{
    return sendAll(Message, 7168);
}

bool Client::receiveBytes(byte* Output)
{
    std::uint16_t msgLength = 0;
    if (!receiveAll(&msgLength, sizeof(msgLength)) || msgLength > 7168)
        return false;
    return receiveAll(Output, msgLength);
}

bool Client::sendAll(const void* data, std::size_t length)
{
    const char* bytes = static_cast<const char*>(data);
    std::size_t offset = 0;
    while (offset < length)
    {
        int sent = send(server, bytes + offset, static_cast<int>(length - offset), 0);
        if (sent <= 0)
            return false;
        offset += static_cast<std::size_t>(sent);
    }
    return true;
}

bool Client::receiveAll(void* data, std::size_t length)
{
    char* bytes = static_cast<char*>(data);
    std::size_t offset = 0;
    while (offset < length)
    {
        int received = recv(server, bytes + offset, static_cast<int>(length - offset), 0);
        if (received <= 0)
            return false;
        offset += static_cast<std::size_t>(received);
    }
    return true;
}

void Client::close()
{
    if (server != INVALID_SOCKET)
    {
        closesocket(server);
        server = INVALID_SOCKET;
    }
    WSACleanup();
}
