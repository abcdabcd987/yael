// How to run:
//     ./test-many-clients listen 10086
//     ./test-many-clients clients localhost 10086 200
// Try multiple times. Sometimes it'll fail and the server will say
//     Discarded event as socket listner doesn't exist
// This is because the event loop first accept the connection,
// then calls on_new_connection to setup a socket listener.
// When messages come in before the socket listener is set up
// (calls to make_socket_listener or register_socket_listener),
// they will be discarded.

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <chrono>
#include <string>
#include <iostream>
#include <unordered_map>
#include <glog/logging.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "yael/NetworkSocketListener.h"
#include "yael/EventLoop.h"

using namespace yael;
using namespace std::chrono_literals;

class Acceptor : public yael::NetworkSocketListener
{
public:
    Acceptor(uint16_t port);

protected:
    void on_new_connection(std::unique_ptr<yael::network::Socket> &&socket) override;
};

class Peer : public yael::NetworkSocketListener
{
public:
    Peer(const std::string &host, uint16_t port);
    Peer(std::unique_ptr<yael::network::Socket> &&s);
    void send(const std::string &msg);
    bool done = false;

protected:
    void on_network_message(yael::network::Socket::message_in_t &msg) override;
};

std::string to_string(yael::network::Socket::message_in_t &msg)
{
    return std::string(reinterpret_cast<const char*>(msg.data), msg.length);
}

void Acceptor::on_new_connection(std::unique_ptr<yael::network::Socket> &&socket)
{
    auto &el = EventLoop::get_instance();
    el.make_socket_listener<Peer>(std::move(socket));
}

Acceptor::Acceptor(uint16_t port)
{
    const std::string host = "0.0.0.0";
    auto socket = new yael::network::Socket();
    bool res = socket->listen(host, port, 100);
    if(!res)
        throw std::runtime_error("socket->listen failed");
    yael::NetworkSocketListener::set_socket(std::unique_ptr<network::Socket>{socket}, yael::SocketType::Acceptor);
    LOG(INFO) << "Listening for peers on host " << host << " port " << port;
}

Peer::Peer(const std::string &host, uint16_t port)
    : yael::NetworkSocketListener(nullptr, yael::SocketType::Connection)
{
    auto sock = new yael::network::Socket();
    auto addr = yael::network::resolve_URL(host, port);
    bool success = sock->connect(addr);
    if (!success)
        throw std::runtime_error("Failed to connect to other server");
    NetworkSocketListener::set_socket(std::unique_ptr<network::Socket>{sock}, SocketType::Connection);
    LOG(INFO) << "connected to " << host << ":" << port;
}

Peer::Peer(std::unique_ptr<yael::network::Socket> &&s)
    : yael::NetworkSocketListener(std::forward<std::unique_ptr<yael::network::Socket>>(s), yael::SocketType::Connection)
{
    LOG(INFO) << "new peer connected";
}

void Peer::send(const std::string &msg)
{
    const uint8_t *data = reinterpret_cast<const uint8_t*>(msg.c_str());
    const uint32_t length = msg.size();
    bool result = socket().send(data, length);
    if (!result)
        throw std::runtime_error("Failed to send message");
}

void Peer::on_network_message(yael::network::Socket::message_in_t &msg)
{
    std::string message = to_string(msg);
    LOG(INFO) << "got message: " << message;
    if (message == "ping")
        send("pong");
    else if (message == "pong")
    {
        std::this_thread::sleep_for(10ms);
        done = true;
    }
}

void stop_handler(int)
{
    LOG(INFO) << "Received signal. Stopping...";
    yael::EventLoop::get_instance().stop();
}

void do_child(const std::string &host, uint16_t port)
{
    FLAGS_logbufsecs = 0; 
    FLAGS_logbuflevel = google::GLOG_INFO;
    FLAGS_logtostderr = 1;
    google::InitGoogleLogging("do_child");

    yael::EventLoop::initialize();
    auto &event_loop = yael::EventLoop::get_instance();
    signal(SIGSTOP, stop_handler);
    signal(SIGTERM, stop_handler);

    std::thread t([&host, port] {
        std::this_thread::sleep_for(100ms);
        auto &el = EventLoop::get_instance();
        auto c = el.make_socket_listener<Peer>(host, port);
        c->send("ping");
        while (!c->done)
            std::this_thread::sleep_for(10ms);
        el.stop();
    });

    event_loop.wait();
    event_loop.destroy();
    t.join();
}

void do_connect(int argc, char** argv)
{
    const std::string &host = argv[2];
    const uint16_t port = std::atoi(argv[3]);
    const int num = std::atoi(argv[4]);
    
    for (int i = 0; i < num; ++i)
    {
        pid_t pid = fork();
        if (pid < 0)
        {
            perror("error");
            abort();
        }
        else if (pid == 0)
        {
            do_child(host, port);
            exit(0);
        }
    }

    bool ok = true;
    for (int i = 0; i < num; ++i)
    {
        int status;
        pid_t pid = wait(&status);
        ok = ok && status == 0;
        printf("[%d/%d] Child with PID %ld exited with status 0x%x.\n", i+1, num, (long)pid, status);
    }
    if (ok)
        puts("All Done!");
    else
    {
        puts("Failed!");
        exit(1);
    }
}

void do_listen(int argc, char** argv)
{
    FLAGS_logbufsecs = 0; 
    FLAGS_logbuflevel = google::GLOG_INFO;
    FLAGS_logtostderr = 1;
    google::InitGoogleLogging("do_listen");

    yael::EventLoop::initialize();
    auto &event_loop = yael::EventLoop::get_instance();
    signal(SIGSTOP, stop_handler);
    signal(SIGTERM, stop_handler);

    const uint16_t port = std::atoi(argv[2]);
    event_loop.make_socket_listener<Acceptor>(port);
    event_loop.wait();
    event_loop.destroy();
}

void print_help()
{
    std::cout << "usage: " << std::endl
              << "   ./mutex-assert-bug listen  <listen-port>" << std::endl
              << "   ./mutex-assert-bug connect <upstream-host> <upstream-port> <num_connection>" << std::endl
              << std::endl;
    exit(1);
}

int main(int argc, char** argv)
{
    if (argc < 3)
        print_help();
    if (!strcmp(argv[1], "listen") && argc == 3)
        do_listen(argc, argv);
    else if (!strcmp(argv[1], "connect") && argc == 5)
        do_connect(argc, argv);
    else
        print_help();
}
