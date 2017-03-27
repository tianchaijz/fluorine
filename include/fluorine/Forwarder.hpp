#pragma once

#include <string>
#include <iostream>

#include "snet/Acceptor.h"
#include "snet/Connector.h"
#include "snet/Connection.h"
#include "snet/EventLoop.h"
#include "snet/Timer.h"

namespace fluorine {
namespace forwarder {

class Connection final {
public:
  using ErrorHandler = std::function<void()>;
  using DataHandler  = std::function<void(std::unique_ptr<snet::Buffer>)>;

  Connection(std::unique_ptr<snet::Connection> connection);

  Connection(const Connection &) = delete;
  void operator=(const Connection &) = delete;

  void SetErrorHandler(const ErrorHandler &error_handler);
  void SetDataHandler(const DataHandler &data_handler);
  int Send(std::unique_ptr<snet::Buffer> buffer);
  bool CanSend() { return connection_ && !connection_->SendQueueFull(); }

private:
  int SendBuffer(std::unique_ptr<snet::Buffer> buffer);
  void HandleError();
  void HandleReceivable();

  static const int kLengthBytes = 1024;

  char recv_length_[kLengthBytes];
  snet::Buffer recv_length_buffer_;

  ErrorHandler error_handler_;
  DataHandler data_handler_;
  std::unique_ptr<snet::Connection> connection_;
};

class Backend final {
public:
  using OnConnected  = std::function<void()>;
  using ErrorHandler = std::function<void()>;
  using DataHandler  = std::function<void(std::unique_ptr<snet::Buffer>)>;

  Backend(const std::string &ip, unsigned short port, snet::EventLoop *loop);

  Backend(const Backend &) = delete;
  void operator=(const Backend &) = delete;

  void SetErrorHandler(const ErrorHandler &error_handler);
  void SetDataHandler(const DataHandler &data_handler);
  void Connect(const OnConnected &onc);
  bool Send(std::unique_ptr<snet::Buffer> buffer);
  bool CanSend() { return connection_->CanSend(); }

private:
  void HandleConnect(std::unique_ptr<snet::Connection> connection,
                     const OnConnected &onc);

  ErrorHandler error_handler_;
  DataHandler data_handler_;

  snet::Connector connector_;
  std::unique_ptr<Connection> connection_;
};

class Frontend final {
public:
  Frontend(const std::string &backend_ip, unsigned short backend_port,
           snet::EventLoop *loop, snet::TimerList *timer_list)
      : backend_port_(backend_port), backend_ip_(backend_ip), loop_(loop),
        backend_reconnect_timer_(timer_list), enable_send_(false) {
    CreateTunnel();
  }

  Frontend(const Frontend &) = delete;
  void operator=(const Frontend &) = delete;

  bool IsEnableSend() { return enable_send_; }
  bool CanSend() { return IsEnableSend() && backend_->CanSend(); }
  void Send(std::unique_ptr<snet::Buffer> data) {
    backend_->Send(std::move(data));
  }

private:
  void CreateTunnel();
  void HandleTunnelError();
  void HandleTunnelData(std::unique_ptr<snet::Buffer> data);
  void HandleTunnelConnected() { enable_send_ = true; }

  unsigned short backend_port_;
  std::string backend_ip_;

  snet::EventLoop *loop_;

  snet::Timer backend_reconnect_timer_;
  std::unique_ptr<Backend> backend_;
  bool enable_send_;
};
} // namespace forwarder
} // namespace fluorine
