#include "Connection.h"

#include <afina/execute/Command.h>
#include <iostream>
#include <sys/socket.h>
#include <unistd.h>

namespace Afina {
namespace Network {
namespace STcoroutine {

void Connection::Start() {
    _logger->debug("Connection on {} socket started", _socket);
    _event.data.fd = _socket;
    _event.data.ptr = this;
    _event.events = EPOLLIN | EPOLLHUP | EPOLLERR;
    _is_alive = true;
}

// See Connection.h
void Connection::OnError() {
    _logger->warn("Connection on {} socket has error", _socket);
    _is_alive = false;
}

// See Connection.h
void Connection::OnClose() {
    _logger->debug("Connection on {} socket closed", _socket);
    _is_alive = false;
}

// See Connection.h
void Connection::DoReadWrite() {
    _logger->debug("Do read on {} socket", _socket);
    char _read_buffer[4096] = "";
    size_t _read_bytes = 0;
    std::size_t _arg_remains = 0;
    Protocol::Parser _parser;
    std::string _argument_for_command;
    std::unique_ptr<Execute::Command> _command_to_execute;

    try {
        int read_count = -1;
        while ((read_count = read(_socket, _read_buffer + _read_bytes, sizeof(_read_buffer) - _read_bytes)) > 0) {
            _read_bytes += read_count;
            _logger->debug("Got {} bytes from socket", read_count);

            while (_read_bytes > 0) {
                _logger->debug("Process {} bytes", _read_bytes);
                // There is no command yet
                if (!_command_to_execute) {
                    std::size_t parsed = 0;
                    try {
                        if (_parser.Parse(_read_buffer, _read_bytes, parsed)) {
                            // There is no command to be launched, continue to parse input stream
                            // Here we are, current chunk finished some command, process it
                            _logger->debug("Found new command: {} in {} bytes", _parser.Name(), parsed);
                            _command_to_execute = _parser.Build(_arg_remains);
                            if (_arg_remains > 0) {
                                _arg_remains += 2;
                            }
                        }
                    } catch (std::runtime_error &ex) {
                        _event.events |= EPOLLOUT;
                        throw std::runtime_error(ex.what());
                    }

                    // Parsed might fails to consume any bytes from input stream. In real life that could happens,
                    // for example, because we are working with UTF-16 chars and only 1 byte left in stream
                    if (parsed == 0) {
                        break;
                    } else {
                        std::memmove(_read_buffer, _read_buffer + parsed, _read_bytes - parsed);
                        _read_bytes -= parsed;
                    }
                }

                // There is command, but we still wait for argument to arrive...
                if (_command_to_execute && _arg_remains > 0) {
                    _logger->debug("Fill argument: {} bytes of {}", _read_bytes, _arg_remains);
                    // There is some parsed command, and now we are reading argument
                    std::size_t to_read = std::min(_arg_remains, std::size_t(_read_bytes));
                    _argument_for_command.append(_read_buffer, to_read);

                    std::memmove(_read_buffer, _read_buffer + to_read, _read_bytes - to_read);
                    _arg_remains -= to_read;
                    _read_bytes -= to_read;
                }

                // There is command & argument - RUN!
                if (_command_to_execute && _arg_remains == 0) {
                    _logger->debug("Start command execution");

                    std::string result;
                    _command_to_execute->Execute(*_pStorage, _argument_for_command, result);

                    // Send response
                    result += "\r\n";

                    _event.events |= EPOLLOUT;

                    // return to run ServerImpl::OnRun()
                    _engine->block();

                    // the second entry to DoReadWrite() method
                    if (send(_socket, result.data(), result.size(), 0) <= 0) {
                        throw std::runtime_error("Failed to send response");
                    }

                    // Prepare for the next command
                    _command_to_execute.reset();
                    _argument_for_command.resize(0);
                    _parser.Reset();

                    // return to run ServerImpl::OnRun()
                    _engine->block();
                }
            }
        } // while (read_count)
        if (read_count == 0) {
            _logger->debug("Connection closed");
            _is_alive = false;
        } else {
            throw std::runtime_error(std::string(strerror(errno)));
        }
    } catch (std::runtime_error &ex) {
        _logger->error("Failed to process connection on descriptor {}: {}", _socket, ex.what());
        _is_alive = false;
    }
}

} // namespace STcoroutine
} // namespace Network
} // namespace Afina
