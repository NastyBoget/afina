#ifndef AFINA_NETWORK_ST_COROUTINE_CONNECTION_H
#define AFINA_NETWORK_ST_COROUTINE_CONNECTION_H

#include <cstring>

#include <afina/Storage.h>
#include <afina/coroutine/Engine.h>
#include <protocol/Parser.h>
#include <spdlog/logger.h>
#include <sys/epoll.h>

namespace Afina {
namespace Network {
namespace STcoroutine {

class Connection {
public:
    Connection(int s, std::shared_ptr<Afina::Storage> &ps, std::shared_ptr<spdlog::logger> &pl) : _socket(s), _is_alive(false),
   _ctx(nullptr), _pStorage(ps), _logger(pl) {}

    inline bool isAlive() const { return _is_alive; }

    void Start();

protected:
    void OnError();
    void OnClose();
    void DoReadWrite(Afina::Coroutine::Engine &engine);

private:
    friend class ServerImpl;

    int _socket;

    bool _is_alive;

    Afina::Coroutine::Engine::context *_ctx;

    struct epoll_event _event;

    std::shared_ptr<spdlog::logger> _logger;
    std::shared_ptr<Afina::Storage> _pStorage;

};
} // namespace STcoroutine
} // namespace Network
} // namespace Afina

#endif // AFINA_NETWORK_ST_COROUTINE_CONNECTION_H
