// Copyright 2020 Your Name <your_email>

#ifndef INCLUDE_SERVER_HELPER_HPP_
#define INCLUDE_SERVER_HELPER_HPP_

#include <beast.hpp>
#include <string>
#include <nlohmann/json.hpp>

namespace http = beast::http;
using nlohmann::json;

template<class Stream>
struct send_lambda
{
  Stream& stream_;
  bool& close_;
  beast::error_code& ec_;

  explicit
  send_lambda(Stream& stream, bool& close,
      beast::error_code& ec);

  template<bool isRequest, class Body, class Fields>
  void operator()(http::message<isRequest, Body, Fields>&& msg) const;
};

template<class Stream>
send_lambda<Stream>::send_lambda(
      Stream& stream,
      bool& close,
      beast::error_code& ec)
      : stream_(stream)
      , close_(close)
      , ec_(ec) {}


template<class Stream>
template<bool isRequest, class Body, class Fields>
void send_lambda<Stream>::operator()(http::message<isRequest, Body, Fields>&& msg) const
{
   // Determine if we should close the connection after
  close_ = !msg.keep_alive();
  // We need the serializer here because the serializer requires
  // a non-const body, and the message oriented version of
  // http::write only works with const messages.
  http::serializer<isRequest, Body, Fields> sr{msg};
  http::write(stream_, sr, ec_);
}


struct suggest{
  std::string id;
  std::string name;
  int cost;
};

void from_json(const json& j, suggest& s);

struct result{
  std::string name;
  int cost;
  int position;

  explicit result(std::string& name, int cost);

  bool operator <(const result& r) const;
};

void to_json(json& j, const result& s);

#endif // INCLUDE_SERVER_HELPER_HPP_
