/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements. See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership. The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License. You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied. See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#ifndef CUEHTTP_CONNECTION_HPP_
#define CUEHTTP_CONNECTION_HPP_

#include <memory>
#include <functional>
#include <queue>
#include <mutex>
#include <boost/asio.hpp>

#include "cuehttp/detail/http_parser.hpp"
#include "cuehttp/detail/noncopyable.hpp"
#include "cuehttp/detail/endian.hpp"

namespace cue {
namespace http {
namespace detail {

template <typename Socket, typename T>
class base_connection : public std::enable_shared_from_this<base_connection<Socket, T>>, safe_noncopyable {
public:
    template <typename S = Socket, typename = std::enable_if_t<std::is_same<std::decay_t<S>, http_socket>::value>>
    base_connection(std::function<void(context&)> handler, boost::asio::io_service& io_service) noexcept
        : socket_{io_service},
          parser_{std::bind(&base_connection::reply_chunk, this, std::placeholders::_1), false,
                  std::bind(&base_connection::send_ws_frame, this, std::placeholders::_1)},
          handler_{std::move(handler)} {
    }

    virtual ~base_connection() = default;

#ifdef ENABLE_HTTPS
    template <typename S = Socket, typename = std::enable_if_t<!std::is_same<std::decay_t<S>, http_socket>::value>>
    base_connection(std::function<void(context&)> handler, boost::asio::io_service& io_service,
                    boost::asio::ssl::context& ssl_context) noexcept
        : socket_{io_service, ssl_context},
          parser_{std::bind(&base_connection::reply_chunk, this, std::placeholders::_1), true,
                  std::bind(&base_connection::send_ws_frame, this, std::placeholders::_1)},
          handler_{std::move(handler)} {
    }
#endif // ENABLE_HTTPS

    boost::asio::ip::tcp::socket& socket() noexcept {
        return static_cast<T&>(*this).socket();
    }

    void run() {
        do_read();
    }

protected:
    void close() {
        if (ws_handshake_) {
            ws_helper_->websocket_->emit(detail::ws_event::close);
            ws_handshake_ = false;
        }
        boost::system::error_code code;
        socket().shutdown(boost::asio::ip::tcp::socket::shutdown_both, code);
        socket().close(code);
    }

    void do_read() {
        static_cast<T&>(*this).do_read_real();
    }

    void do_read_some() {
        auto buffer = parser_.buffer();
        socket_.async_read_some(boost::asio::buffer(buffer.first, buffer.second),
                                [this, self = this->shared_from_this()](const boost::system::error_code& code,
                                                                        std::size_t bytes_transferred) {
                                    if (code) {
                                        close();
                                    } else {
                                        const auto parse_code = parser_.parse(bytes_transferred);
                                        // =  0 success
                                        // = -1 error
                                        // = -2 not complete
                                        switch (parse_code) {
                                        case 0:
                                            handle_and_reply();
                                            break;
                                        case -1:
                                            reply_error(400);
                                            break;
                                        case -2:
                                        default:
                                            do_read();
                                        }
                                    }
                                });
    }

    void reply_error(unsigned status) {
        reply(make_reply_str(status), false);
    }

    void handle_and_reply() {
        assert(handler_);
        std::string buffers;
        buffers.reserve(2048);
        bool is_close{false};
        for (auto& ctx : parser_.contexts()) {
            handler_(*ctx);
            auto& req = ctx->req();
            auto& res = ctx->res();
            res.major_version(req.major_version());
            res.minor_version(req.minor_version());
            res.keepalive(req.keepalive());
            if (req.websocket() && !ws_helper_) {
                ws_helper_ = std::make_unique<ws_helper>();
                ws_helper_->websocket_ = ctx->websocket_ptr();
            }
            is_close = is_close || !res.keepalive();
            if (!res.is_stream()) {
                buffers.append(res.to_string());
            }
        }
        if (!buffers.empty()) {
            reply(buffers, is_close);
        }
    }

    void reply(const std::string& buffers, bool is_close) {
        boost::asio::async_write(socket_, boost::asio::buffer(buffers),
                                 [is_close, this, self = this->shared_from_this()](
                                     const boost::system::error_code& code, std::size_t bytes_transferred) {
                                     if (code || is_close) {
                                         close();
                                     } else {
                                         if (ws_helper_) {
                                             ws_handshake_ = true;
                                             ws_helper_->websocket_->emit(detail::ws_event::open);
                                             do_read_ws_header();
                                         } else {
                                             parser_.reset();
                                             do_read();
                                         }
                                     }
                                 });
    }

    bool reply_chunk(const std::string& chunk) {
        boost::system::error_code code;
        boost::asio::write(socket_, boost::asio::buffer(chunk), code);
        return !!code;
    }

    void do_read_ws_header() {
        boost::asio::async_read(socket_, boost::asio::buffer(ws_helper_->ws_reader_.header),
                                [this, self = this->shared_from_this()](const boost::system::error_code& code,
                                                                        std::size_t bytes_transferred) {
                                    if (code) {
                                        close();
                                    } else {
                                        auto& reader = ws_helper_->ws_reader_;
                                        reader.fin = reader.header[0] & 0x80;
                                        reader.opcode = static_cast<detail::ws_opcode>(reader.header[0] & 0xf);
                                        reader.has_mask = reader.header[1] & 0x80;
                                        reader.length = reader.header[1] & 0x7f;
                                        if (reader.length == 126) {
                                            do_read_ws_length_and_mask(2);
                                        } else if (reader.length == 127) {
                                            do_read_ws_length_and_mask(8);
                                        } else {
                                            do_read_ws_length_and_mask(0);
                                        }
                                    }
                                });
    }

    void do_read_ws_length_and_mask(std::size_t bytes) {
        auto& reader = ws_helper_->ws_reader_;
        const auto length = bytes + (reader.has_mask ? 4 : 0);
        if (length == 0) {
            handle_ws();
        } else {
            reader.length_mask_buffer.resize(length);
            boost::asio::async_read(
                socket_, boost::asio::buffer(reader.length_mask_buffer.data(), length),
                [bytes, this, self = this->shared_from_this()](const boost::system::error_code& code,
                                                               std::size_t bytes_transferred) {
                    if (code) {
                        close();
                    } else {
                        auto& reader = ws_helper_->ws_reader_;
                        if (bytes == 2) {
                            reader.length =
                                detail::from_be(*reinterpret_cast<std::uint16_t*>(reader.length_mask_buffer.data()));
                            if (reader.has_mask) {
                                memcpy(reader.mask, reader.length_mask_buffer.data() + 2, 4);
                            }
                        } else if (bytes == 8) {
                            reader.length =
                                detail::from_be(*reinterpret_cast<std::uint64_t*>(reader.length_mask_buffer.data()));
                            if (reader.has_mask) {
                                memcpy(reader.mask, reader.length_mask_buffer.data() + 8, 4);
                            }
                        } else {
                            if (reader.has_mask) {
                                memcpy(reader.mask, reader.length_mask_buffer.data(), 4);
                            }
                        }
                        do_read_ws_payload();
                    }
                });
        }
    }

    void do_read_ws_payload() {
        auto& reader = ws_helper_->ws_reader_;
        const std::size_t length{reader.payload_buffer.size()};
        reader.payload_buffer.resize(reader.length + length);
        boost::asio::async_read(socket_, boost::asio::buffer(reader.payload_buffer.data() + length, reader.length),
                                [this, self = this->shared_from_this()](const boost::system::error_code& code,
                                                                        std::size_t bytes_transferred) {
                                    if (code) {
                                        close();
                                    } else {
                                        auto& reader = ws_helper_->ws_reader_;
                                        auto& payload_buffer = reader.payload_buffer;
                                        if (reader.has_mask) {
                                            for (std::size_t i{0}; i < reader.length; ++i) {
                                                payload_buffer[i] ^= reader.mask[i % 4];
                                            }
                                        }
                                        handle_ws();
                                    }
                                });
    }

    void handle_ws() {
        auto& reader = ws_helper_->ws_reader_;
        switch (reader.opcode) {
        case detail::ws_opcode::continuation:
            reader.last_fin = false;
            break;
        case detail::ws_opcode::text:
        case detail::ws_opcode::binary: {
            if (reader.fin) {
                reader.last_fin = true;
                auto& payload_buffer = reader.payload_buffer;
                ws_helper_->websocket_->emit(detail::ws_event::msg, {payload_buffer.data(), payload_buffer.size()});
                payload_buffer.clear();
            } else {
                reader.last_fin = false;
            }
            break;
        }
        case detail::ws_opcode::close:
            close();
            return;
        case detail::ws_opcode::ping:
            reply_ws_pong();
            break;
        default:
            break;
        }
        do_read_ws_header();
    }

    void reply_ws_pong() {
        detail::ws_frame frame;
        frame.opcode = detail::ws_opcode::pong;
        send_ws_frame(std::move(frame));
    }

    void send_ws_frame(detail::ws_frame&& frame) {
        std::unique_lock<std::mutex> lock{ws_helper_->write_queue_mutex_};
        ws_helper_->write_queue_.emplace(std::move(frame));

        if (ws_helper_->write_queue_.size() == 1 && ws_handshake_) {
            lock.unlock();
            do_send_ws_frame();
        }
    }

    void do_send_ws_frame() {
        auto& frame = get_frame();
        std::ostream os{&ws_helper_->buffer_};
        // opcode
        auto opcode = static_cast<std::uint8_t>(frame.opcode) | 0x80;
        os.write(reinterpret_cast<char*>(&opcode), 1);
        // length
        std::uint8_t base_length{0};
        std::uint16_t length16{0};
        std::uint64_t length64{0};
        const auto size = frame.payload.size();
        if (size < 126) {
            base_length |= static_cast<std::uint8_t>(size);
            os.write(reinterpret_cast<char*>(&base_length), sizeof(std::uint8_t));
        } else if (size <= UINT16_MAX) {
            base_length |= 0x7e;
            os.write(reinterpret_cast<char*>(&base_length), sizeof(std::uint8_t));
            length16 = detail::to_be(static_cast<std::uint16_t>(size));
            os.write(reinterpret_cast<char*>(&length16), sizeof(std::uint16_t));
        } else {
            base_length |= 0x7f;
            os.write(reinterpret_cast<char*>(&base_length), sizeof(std::uint8_t));
            length64 = detail::to_be(static_cast<std::uint64_t>(size));
            os.write(reinterpret_cast<char*>(&length64), sizeof(std::uint64_t));
        }

        if (size > 0) {
            // payload
            auto& payload = frame.payload;
            os.write(payload.data(), payload.size());
        }

        do_write_ws();
    }

    void do_write_ws() {
        boost::asio::async_write(socket_, ws_helper_->buffer_,
                                 [this, self = this->shared_from_this()](const boost::system::error_code& code,
                                                                         std::size_t bytes_transferred) {
                                     if (code) {
                                         close();
                                     } else {
                                         std::unique_lock<std::mutex> lock{ws_helper_->write_queue_mutex_};
                                         ws_helper_->write_queue_.pop();
                                         if (!ws_helper_->write_queue_.empty()) {
                                             lock.unlock();
                                             do_send_ws_frame();
                                         }
                                     }
                                 });
    }

    detail::ws_frame& get_frame() {
        std::unique_lock<std::mutex> lock{ws_helper_->write_queue_mutex_};
        assert(!ws_helper_->write_queue_.empty());
        return ws_helper_->write_queue_.front();
    }

    std::string make_reply_str(unsigned status) const {
        std::string str;
        str.append("HTTP/1.1 ");
        str.append(std::to_string(status));
        str.append(" ");
        auto message = detail::utils::get_message_for_status(status);
        str.append(message.data(), message.size());
        return str;
    }

    struct ws_helper final {
        std::shared_ptr<websocket> websocket_;
        boost::asio::streambuf buffer_;
        detail::ws_reader ws_reader_;
        std::queue<detail::ws_frame> write_queue_;
        std::mutex write_queue_mutex_;
    };

    Socket socket_;
    detail::http_parser parser_;
    std::function<void(context&)> handler_;
    bool ws_handshake_{false};
    std::unique_ptr<ws_helper> ws_helper_;
};

template <typename Socket = http_socket>
class connection final : public base_connection<Socket, connection<Socket>>, safe_noncopyable {
public:
    template <typename... Args>
    connection(Args&&... args) noexcept : base_connection<Socket, connection<Socket>>{std::forward<Args>(args)...} {
    }

    boost::asio::ip::tcp::socket& socket() noexcept {
        return this->socket_;
    }

    void do_read_real() {
        this->do_read_some();
    }
};

#ifdef ENABLE_HTTPS
template <>
class connection<https_socket> final : public base_connection<https_socket, connection<https_socket>>,
                                       safe_noncopyable {
public:
    template <typename... Args>
    connection(Args&&... args) noexcept : base_connection{std::forward<Args>(args)...} {
    }

    boost::asio::ip::tcp::socket& socket() noexcept {
        return socket_.next_layer();
    }

    void do_read_real() {
        if (has_handshake_) {
            do_read_some();
        } else {
            do_handshake();
        }
    }

private:
    void do_handshake() {
        socket_.async_handshake(boost::asio::ssl::stream_base::server,
                                [this, self = this->shared_from_this()](const boost::system::error_code& code) {
                                    if (code) {
                                        close();
                                    } else {
                                        has_handshake_ = true;
                                        do_read_some();
                                    }
                                });
    }

    bool has_handshake_{false};
};
#endif // ENABLE_HTTPS

} // namespace detail
} // namespace http
} // namespace cue

#endif // CUEHTTP_CONNECTION_HPP_
