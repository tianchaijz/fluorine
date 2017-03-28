#include <string.h>
#include <iostream>

#include "spdlog/spdlog.h"
#include "fluorine/Forwarder.hpp"

static auto logger = spdlog::stdout_color_st("Forwarder");

namespace fluorine {
namespace forwarder {

Connection::Connection(std::unique_ptr<snet::Connection> connection)
    : recv_length_buffer_(recv_length_, sizeof(recv_length_)),
      connection_(std::move(connection)) {
  memset(recv_length_, 0, sizeof(recv_length_));

  connection_->SetOnError([this]() { HandleError(); });
  connection_->SetOnReceivable([this]() { HandleReceivable(); });
}

void Connection::SetErrorHandler(const ErrorHandler &error_handler) {
  error_handler_ = error_handler;
}

void Connection::SetDataHandler(const DataHandler &data_handler) {
  data_handler_ = data_handler;
}

int Connection::Send(std::unique_ptr<snet::Buffer> buffer) {
  return SendBuffer(std::move(buffer));
}

int Connection::SendBuffer(std::unique_ptr<snet::Buffer> buffer) {
  if (connection_->Send(std::move(buffer)) ==
      static_cast<int>(snet::SendE::Error)) {
    logger->error("send error");
    error_handler_();
    return false;
  }

  return true;
}

void Connection::HandleError() { error_handler_(); }

void Connection::HandleReceivable() {
  if (recv_length_buffer_.pos < recv_length_buffer_.size) {
    auto ret = connection_->Recv(&recv_length_buffer_);
    if (ret == static_cast<int>(snet::RecvE::PeerClosed) ||
        ret == static_cast<int>(snet::RecvE::Error)) {
      error_handler_();
      return;
    }

    if (ret > 0) {
      recv_length_buffer_.pos += ret;
    }
  }

  auto length = recv_length_buffer_.pos;
  auto data   = new char[length];
  std::copy(recv_length_buffer_.buf, recv_length_buffer_.buf + length, data);
  std::unique_ptr<snet::Buffer> ptr(
      new snet::Buffer(data, length, snet::OpDeleter));
  recv_length_buffer_.pos = 0;
  data_handler_(std::move(ptr));
}

Backend::Backend(const std::string &ip, unsigned short port,
                 snet::EventLoop *loop)
    : connector_(ip, port, loop) {}

void Backend::SetErrorHandler(const ErrorHandler &error_handler) {
  error_handler_ = error_handler;
}

void Backend::SetDataHandler(const DataHandler &data_handler) {
  data_handler_ = data_handler;
}

void Backend::Connect(const OnConnected &onc) {
  connector_.Connect([this, onc](std::unique_ptr<snet::Connection> connection) {
    HandleConnect(std::move(connection), onc);
  });
}

bool Backend::Send(std::unique_ptr<snet::Buffer> buffer) {
  return connection_->Send(std::move(buffer));
}

void Backend::HandleConnect(std::unique_ptr<snet::Connection> connection,
                            const OnConnected &onc) {
  if (connection) {
    logger->info("connect to backend success");
    connection_.reset(new Connection(std::move(connection)));
    connection_->SetErrorHandler(error_handler_);
    connection_->SetDataHandler(data_handler_);
    onc();
  } else {
    error_handler_();
  }
}

void Frontend::CreateTunnel() {
  logger->warn("create new tunnel to {}:{}", backend_ip_, backend_port_);
  backend_.reset(new Backend(backend_ip_, backend_port_, loop_));
  backend_->SetErrorHandler([this]() { HandleTunnelError(); });
  backend_->SetDataHandler([this](std::unique_ptr<snet::Buffer> data) {
    HandleTunnelData(std::move(data));
  });
  backend_->Connect([this]() { HandleTunnelConnected(); });
}

void Frontend::HandleTunnelError() {
  enable_send_ = false;
  backend_reconnect_timer_.ExpireFromNow(snet::Seconds(1));
  backend_reconnect_timer_.SetOnTimeout([this]() { CreateTunnel(); });
}

void Frontend::HandleTunnelData(std::unique_ptr<snet::Buffer> data) {
  logger->error("received from tunnel: {}",
                std::string(data->buf, data->buf + data->pos));
}

} // namespace forwarder
} // namespace fluorine
