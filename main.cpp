#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio.hpp>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <deque>
#include <set>

#include "chat_message.hpp"

namespace beast = boost::beast;         // from <boost/beast.hpp>
namespace http = beast::http;           // from <boost/beast/http.hpp>
namespace websocket = beast::websocket; // from <boost/beast/websocket.hpp>
namespace net = boost::asio;            // from <boost/asio.hpp>

using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>

typedef std::deque<std::string> chat_message_que;

class chat_participant {
public:
    virtual ~chat_participant() = default;
    virtual void deliver(const std::string& message) = 0;
};

typedef std::shared_ptr<chat_participant> chat_participant_ptr;

class chat_room {
public:
    void join(const chat_participant_ptr& chat_participant) {
        participants_.insert(chat_participant);
        std::cout << "inserted user";
    }

    void leave(const chat_participant_ptr& participant)
    {
        participants_.erase(participant);
    }

    void deliver(const std::string& message) {
        recent_message.push_back(message);
        while (recent_message.size() > max_recent_message) {
            recent_message.pop_front();
        }
        for(const auto& participant: participants_)
            participant->deliver(message);
    }

private:
    chat_message_que recent_message;
    enum {max_recent_message = 100};
    std::set<chat_participant_ptr> participants_;

};

void
fail(beast::error_code ec, char const* what)
{
    std::cerr << what << ": " << ec.message() << "\n";
}


class session : public std::enable_shared_from_this<session>, public chat_participant {
    websocket::stream<beast::tcp_stream> ws_;
    beast::flat_buffer buffer_;
public:
    explicit session(tcp::socket&& socket, chat_room& room)
    : ws_(std::move(socket)), room_(room)
    {
    }

    void run() {
        room_.join(shared_from_this());
        net::dispatch(ws_.get_executor(),
                      beast::bind_front_handler(&session::on_run, shared_from_this()));
    }

    void on_run() {
        ws_.set_option(websocket::stream_base::timeout::suggested(beast::role_type::server));
        ws_.set_option(websocket::stream_base::decorator([](websocket::response_type& res)
                                                         {
            res.set(http::field::server, std::string(BOOST_BEAST_VERSION_STRING)
            + "websocket-server-async");
                                                         }));
        //accept handshake
        ws_.async_accept(beast::bind_front_handler(&session::on_accept, shared_from_this()));
    }

    void on_accept(beast::error_code ec) {

        if (ec)
            return fail(ec, "accept");

        do_read();
    }

    void deliver(const std::string& msg)
    {
        bool write_in_progress = !write_message.empty();
        write_message.push_back(msg);
        if (!write_in_progress)
        {
            do_write();
        }
    }

    void do_read() {
        auto self(shared_from_this());
        ws_.async_read(buffer_, [this, self](boost::beast::error_code ec, std::size_t size)
                        {
            if(!ec) {
                on_read(ec, size);
                ws_.text(ws_.got_text());
                std::ostringstream os;
                os << boost::beast::make_printable(buffer_.data());
                read_message = os.str();
                write_message.push_front(read_message);
                do_read();
            }
            else {
                room_.leave(shared_from_this());
            }
                        });
    }

    void on_read(boost::beast::error_code ec, std::size_t bytes_trans) {
        if(!ec) {
            std::stringstream message;
            ws_.text(ws_.got_text());
            std::ostringstream os;
            os << boost::beast::make_printable(buffer_.data());
            read_message = os.str();
            room_.deliver(read_message);
            do_read();
        }
        else {
            room_.leave(shared_from_this());
        }
    }

    void do_write() {
        auto self(shared_from_this());
        ws_.async_write(net::buffer(write_message.front().data(), write_message.front().length()),
                        [this, self](boost::system::error_code ec, std::size_t size) {
            if(!ec) {
                write_message.pop_front();
                if (!write_message.empty()) {
                    do_write();
                }
            }
            else {
                room_.leave(shared_from_this());
            }
        });
    }

    std::string read_message;
    chat_room& room_;
    chat_message_que write_message;
};


class listener : public std::enable_shared_from_this<listener> {
    net::io_context& ioc_;
    tcp::acceptor  acceptor_;
public:
    listener(net::io_context& ioc, const tcp::endpoint& endpoint)
    : ioc_(ioc), acceptor_(ioc)
    {
        beast::error_code  ec;
        acceptor_.open(endpoint.protocol(), ec);
        if (ec) {
            fail(ec, "open");
            return;
        }
        acceptor_.set_option(net::socket_base::reuse_address(true), ec);
        if(ec) {
            fail(ec, "address");
            return;
        }
        acceptor_.bind(endpoint, ec);
        if(ec) {
            fail(ec, "bind");
            return;
        }
        acceptor_.listen(net::socket_base::max_listen_connections, ec);
        if(ec) {
            fail(ec, "listen");
            return;
        }
    }
    void run() {
        do_accept();
    }

private:
    void do_accept() {
        acceptor_.async_accept(net::make_strand(ioc_),
                               beast::bind_front_handler(&listener::on_accept,
                                                         shared_from_this()));
    }

    void on_accept(beast::error_code ec, tcp::socket socket) {
        if (ec) {
            fail(ec, "accept");
        }
        else {
            std::make_shared<session>(std::move(socket), room_)->run();
        }
    }

    chat_room room_;
};



int main()
{
    auto const address = net::ip::make_address(reinterpret_cast<const char *>("127.0.0.1"));
    auto const port = static_cast<unsigned short>(8080);
    auto const threads = std::max<int>(1,(1));

    // The io_context is required for all I/O
    //net::io_context ioc{threads};
    net::io_context io_context;
    // Create and launch a listening port
    //listener lis(io_context, tcp::endpoint{address, port});
    std::make_shared<listener>(io_context, tcp::endpoint{address, port})->run();

    // Run the I/O service on the requested number of threads

    io_context.run();

    return EXIT_SUCCESS;
}