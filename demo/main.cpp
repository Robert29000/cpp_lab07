#include <beast.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <thread>
#include <functional>
#include <unordered_map>
#include <set>
#include <string>
#include <fstream>
#include <shared_mutex>
#include <nlohmann/json.hpp>
#include <server_helper.hpp>


namespace http = beast::http;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;
using nlohmann::json;



std::shared_mutex mutex_;
std::unordered_map<std::string, std::set<result>> suggestions;  // id -> set of results

template<class Body, class Allocator, class Send>
void handle_request(http::request<Body, http::basic_fields<Allocator>>&& req,
    Send&& send) {

  // Returns a bad request response
  auto const bad_request = [&req](beast::string_view why) {
    http::response<http::string_body> res{http::status::bad_request,
                                          req.version};
    res.set(http::field::server, BEAST_VERSION_STRING);
    res.set(http::field::content_type, "text/html");
    res.keep_alive(false);
    res.body = std::string(why);
    res.prepare_payload();
    return res;
  };

  // Make sure we can handle the method
  if (req.method() != http::verb::post)
    return send(bad_request("Unknown HTTP-method"));

  // Request path must be absolute and not contain "..".
  if (req.target().empty() || req.target()[0] != '/' ||
      req.target().find("..") != beast::string_view::npos) {
    return send(bad_request("Illegal request-target"));
  }

  std::cout << req.target() << " " << req.body << std::endl;

  if (req.target() != "/v1/api/suggest"){
    return send(bad_request("Not Found"));
  }
  json result_data;
  {
    std::shared_lock lock(mutex_);
    json input = json::parse(req.body);
    std::string id;
    input.at("input").get_to(id);
    auto res = suggestions[id];
    json json_suggests = json::array();
    auto count = 0;
    for (auto r : res){
      json o;
      if (r.position == -1){
        r.position = count;
      }
      o = r;
      json_suggests.push_back(o);
      count++;
    }
    result_data["suggestions"] = json_suggests;
  }

  // Respond to POST request
  http::response<http::string_body> res{http::status::ok, req.version};
  res.set(http::field::server, BEAST_VERSION_STRING);
  res.set(http::field::content_type, "application/json");
  res.body = result_data.dump();
  res.keep_alive(false);
  return send(std::move(res));
}

// This is the C++11 equivalent of a generic lambda.
// The function object is used to send an HTTP message.


void fail(beast::error_code ec, char const* what)
{
  std::cerr << what << ": " << ec.message() << "\n";
}


void do_session(tcp::socket& socket)
{
  bool close = false;
  beast::error_code ec;

  // This buffer is required to persist across reads
  beast::flat_buffer buffer;

  // This lambda is used to send messages
  send_lambda<tcp::socket> lambda{socket, close, ec};

    // Read a request
  http::request<http::string_body> req;
  http::read(socket, buffer, req, ec);

  if(ec == http::error::end_of_stream) {
    socket.shutdown(tcp::socket::shutdown_send, ec);
  }
  if(ec) {
    return fail(ec, "read");
  }

    // Send the response
  handle_request(std::move(req), lambda);

  // Send a TCP shutdown
  socket.shutdown(tcp::socket::shutdown_send, ec);

  // At this point the connection is closed gracefully
}

void update_suggestions(const std::string path){
      for(;;){
        {
          std::unique_lock lock(mutex_);
          std::ifstream file(path);
          json data;
          file >> data;
          std::unordered_map<std::string, std::set<result>> new_suggestions;
          for (const auto& s :data){
              auto sug = s.get<suggest>();
              new_suggestions[sug.id].insert(result{sug.name, sug.cost});
          }
          suggestions = std::move(new_suggestions);
        }
        std::this_thread::sleep_for(std::chrono::minutes(15));
      }
}


int main(int argc, char* argv[])
{
  try
  {
    // Check command line arguments.
    if (argc != 4)
    {
      throw std::runtime_error("Incorrect number of args");
    }
    auto const address = net::ip::address::from_string(argv[1]);
    auto const port = static_cast<unsigned short>(std::atoi(argv[2]));
    auto const doc_root = std::string(argv[3]); // path to suggestions.json
    std::string path_to_data;
    if (doc_root == "/") {
      path_to_data = "suggestions.json";
    } else{
      path_to_data = doc_root + "suggestions.json";
    }
    std::ifstream f(path_to_data);

    if (!f.good()){
      throw std::runtime_error("Incorrect directory with json data file");
    }

    std::thread(&update_suggestions, path_to_data).detach();

    // The io_context is required for all I/O
    net::io_service ioc{1};

    // The acceptor receives incoming connections
    tcp::acceptor acceptor{ioc, {address, port}};
    for(;;)
    {
      // This will receive the new connection
      tcp::socket socket{ioc};

      // Block until we get a connection
      acceptor.accept(socket);

      // Launch the session, transferring ownership of the socket
      std::thread{std::bind(&do_session,
          std::move(socket))}.detach();
    }
  }
  catch (const std::exception& e)
  {
    std::cerr << "Error: " << e.what() << std::endl;
    throw e;
  }
}
