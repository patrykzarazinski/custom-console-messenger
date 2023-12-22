#include "ServerApp.hpp"

#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>  // close, read

#include <cstdint>
#include <cstdio>   //std::perror
#include <cstdlib>  //std::exit, std::atoi
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "builders/ConcreteNetworkCreator.hpp"

namespace {
void clearBuffer(std::string& buffer) {
  // TODO Using std::string::clear or std::string::erase lead to infinity loop
  // probably due to invalidate internal iterators, references, pointers
  for (auto& item : buffer) {
    item = '\0';
  }
}
}  // namespace
namespace app {
Server::Server()
    : networkCreator{std::make_unique<ConcreteNetworkCreator>()},
      epollManager{std::make_unique<EpollManager>()},
      serverIsRunning{false} {}

void Server::run(types::IP ip, types::Port port) {
  serverSocket = networkCreator->createSocket(ip, port);
  epollManager->epoll = networkCreator->createEpoll();

  epollManager->registerSocket(serverSocket);
  receiveLoop();

  close(serverSocket);
  close(epollManager->epoll);
  return;
}

void Server::receiveLoop() {
  const int MAX_EVENTS = 10;
  std::vector<epoll_event> events(MAX_EVENTS);
  serverIsRunning = true;
  while (serverIsRunning) {
    int nfds = epoll_wait(epollManager->epoll, events.data(), MAX_EVENTS, -1);

    if (nfds == -1) {
      std::perror("epoll_wait() failed");
      close(serverSocket);
      close(epollManager->epoll);
      std::exit(EXIT_FAILURE);
    }

    for (int i = 0; i < nfds; i++) {
      int fd = events[i].data.fd;
      if (fd == serverSocket) {
        handleNewConnection();
      } else {
        handleClient(fd);
      }
    }
  }
}

void Server::handleNewConnection() {
  sockaddr_in clientAdress{};
  socklen_t clientAddressLen = sizeof(clientAdress);

  int clientSocket =
      accept4(serverSocket, reinterpret_cast<sockaddr*>(&clientAdress),
              &clientAddressLen, SOCK_NONBLOCK);

  if (clientSocket == -1) {  // add EAGAIN or EWOULDBLOCK handling
    std::perror("accept4() failed");
    close(serverSocket);
    close(epollManager->epoll);
    std::exit(EXIT_FAILURE);
  }

  clientsSocket.push_back(clientSocket);
  epollManager->registerSocket(clientSocket);
}

void Server::handleClient(types::SocketFD clientSocket) {
  std::vector<char> buffer(1024, '\0');
  ssize_t readBytes =
      recv(clientSocket, buffer.data(), buffer.size(), MSG_DONTWAIT);

  if (readBytes == -1) {
    if (readBytes == EAGAIN or readBytes == EWOULDBLOCK) {
      std::cout << "No messages are available at the socket" << std::endl;
      return;
    } else {
      std::perror("recv() failed");
      close(clientSocket);
      std::exit(EXIT_FAILURE);
    }
  } else if (readBytes == 0) {
    std::cout << "Connection closed by peer" << std::endl;
    return;
  } else {
    broadcast(buffer, clientSocket);
  }
}

void Server::broadcast(std::vector<char> buffer, types::SocketFD clientSocket) {
  for (const types::SocketFD& fd : clientsSocket) {
    if (fd != clientSocket) {
      send(fd, buffer.data(), buffer.size(), 0);
    }
  }
}

void Server::EpollManager::registerSocket(types::SocketFD fd) {
  epoll_event event;
  event.events = EPOLLIN;
  event.data.fd = fd;

  if (epoll_ctl(epoll, EPOLL_CTL_ADD, fd, &event) == -1) {
    std::perror("epoll_ctl() failed");
    close(fd);
    std::exit(EXIT_FAILURE);
  }
}

Server::~Server() { std::cout << "Server shutdown" << std::endl; };
}  // namespace app