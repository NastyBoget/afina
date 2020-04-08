#include "Connection.h"

#include <iostream>
#include <sys/socket.h>

namespace Afina {
namespace Network {
namespace STnonblock {

// See Connection.h
void Connection::Start() { 
	_logger->debug("Connection on {} socket started", _socket);
	_event.data.fd = _socket;
	_event.data.ptr = this;
	_event.events = EPOLLIN;
}

// See Connection.h
void Connection::OnError() {
	_logger->warn("Connection on {} socket has error", _socket);std::cout << "OnError" << std::endl;
	is_alive = false;
}

// See Connection.h
void Connection::OnClose() { 
	_logger->debug("Connection on {} socket closed", _socket);
	is_alive = false;
}

// See Connection.h
void Connection::DoRead() { 
	_logger->debug("Do read on {} socket", _socket);

	try{
		int read_bytes = -1;
		while ((read_bytes = read(_socket, _read_buffer + _read_count, 
					sizeof(_read_buffer) - _read_count)) > 0) {
	    	_read_count += read_bytes;
	    	_logger->debug("Got {} bytes from socket", read_bytes);

	    	while (read_bytes > 0) {
	            _logger->debug("Process {} bytes", read_bytes);
	            // There is no command yet
	            if (!command_to_execute) {
	                std::size_t parsed = 0;
	                if (parser.Parse(_read_buffer, read_bytes, parsed)) {
	                    // There is no command to be launched, continue to parse input stream
	                    // Here we are, current chunk finished some command, process it
	                    _logger->debug("Found new command: {} in {} bytes", parser.Name(), parsed);
	                    command_to_execute = parser.Build(arg_remains);
	                    if (arg_remains > 0) {
	                        arg_remains += 2;
	                    }
	                }

	                // Parsed might fails to consume any bytes from input stream. In real life that could happens,
	                // for example, because we are working with UTF-16 chars and only 1 byte left in stream
	                if (parsed == 0) {
	                    break;
	                } else {
	                    std::memmove(_read_buffer, _read_buffer + parsed, read_bytes - parsed);
	                    read_bytes -= parsed;
	                }
	            }

	            // There is command, but we still wait for argument to arrive...
	            if (command_to_execute && arg_remains > 0) {
	                _logger->debug("Fill argument: {} bytes of {}", read_bytes, arg_remains);
	                // There is some parsed command, and now we are reading argument
	                std::size_t to_read = std::min(arg_remains, std::size_t(read_bytes));
	                argument_for_command.append(_read_buffer, to_read);

	                std::memmove(_read_buffer, _read_buffer + to_read, read_bytes - to_read);
	                arg_remains -= to_read;
	                read_bytes -= to_read;
	            }

	            // There is command & argument - RUN!
	            if (command_to_execute && arg_remains == 0) {
	                _logger->debug("Start command execution");

	                std::string result;
	                command_to_execute->Execute(*pStorage, argument_for_command, result);

	                // Send response
	                result += "\r\n";

	                if (_output_queue.empty()) {
	                	_event.events |= EPOLLOUT;
	                }
	                _output_queue.push_back(result);

	                // Prepare for the next command
	                command_to_execute.reset();
	                argument_for_command.resize(0);
	                parser.Reset();
	            }
	        } // while (read_bytes) 
	    }
	    is_alive = false;
	    if (read_bytes == 0) {
                _logger->debug("Connection closed");
            } else {
                throw std::runtime_error(std::string(strerror(errno)));
            }
    } catch (std::runtime_error &ex) {
        _logger->error("Failed to process connection on descriptor {}: {}", _socket, ex.what());
    }

}

// See Connection.h
void Connection::DoWrite() {
	_logger->debug("Do write on {} socket", _socket);
	struct iovec tmp[_output_queue.size()];
	int i = 0;
	for (auto command : _output_queue) {
		tmp[i].iov_base = &command.data();
		tmp[i].iov_len = command.size();
		++i;
	}

	tmp[0].iov_base = static_cast<char *>(tmp[0].iov_base) + _head_written_count;
	tmp[0].iov_len -= _head_written_count;

	int written_bytes = writev(_socket, tmp, i);

	if (written_bytes) {
		is_alive = false;
		throw std::runtime_error("Failed to send response");
	}

	for (auto command : _output_queue) {
		if (written_bytes - command.size() >= 0) {
			_output_queue.pop_front();
			written_bytes -= command.size();
		} else {
			break;
		}
	}
	_head_written_count = written_bytes;

	if (_output_queue.empty()) {
		_event.events = EPOLLIN;
	}
}

} // namespace STnonblock
} // namespace Network
} // namespace Afina
