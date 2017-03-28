#pragma once

#include <string>
#include <iostream>
#include <unordered_map>

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

  bool Send(std::unique_ptr<snet::Buffer> buffer);
  bool CanSend() { return connection_ && !connection_->SendQueueFull(); }

private:
  void HandleError();
  void HandleRecv();

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
  using OnTunnelError     = std::function<void()>;
  using OnTunnelConnected = std::function<void()>;

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

  void SetOnTunnelError(OnTunnelError ote) { ote_ = ote; }
  void SetOnTunnelConnected(OnTunnelError otc) { otc_ = otc; }

private:
  void CreateTunnel();
  void HandleTunnelError();
  void HandleTunnelData(std::unique_ptr<snet::Buffer> data);
  void HandleTunnelConnected() {
    if (otc_) {
      otc_();
    }
    enable_send_ = true;
  }

  OnTunnelError ote_     = nullptr;
  OnTunnelConnected otc_ = nullptr;

  unsigned short backend_port_;
  std::string backend_ip_;

  snet::EventLoop *loop_;

  snet::Timer backend_reconnect_timer_;
  std::unique_ptr<Backend> backend_;
  bool enable_send_;
};

class Client final {
public:
  using OnErrorClose = std::function<void()>;
  using DataHandler  = std::function<void(std::unique_ptr<snet::Buffer>)>;

  explicit Client(std::unique_ptr<snet::Connection> connection);

  Client(const Client &) = delete;
  void operator=(const Client &) = delete;

  void SetOnClose(const OnErrorClose &on_error_close);
  void SetDataHandler(const DataHandler &data_handler);
  void Close();

private:
  void HandleError();
  void HandleRecv();

  static const size_t kBufferSize = 8192;

  OnErrorClose on_error_close_;
  DataHandler data_handler_;

  std::unique_ptr<snet::Buffer> buffer_;
  std::unique_ptr<snet::Connection> connection_;
};

class FrontendServer final {
public:
  using OnNewConnection = std::function<void(std::unique_ptr<Client>)>;

  FrontendServer(const std::string &ip, unsigned short port,
                 snet::EventLoop *loop);

  FrontendServer(const FrontendServer &) = delete;
  void operator=(const FrontendServer &) = delete;

  bool IsListenOk() const;
  void SetOnNewConnection(const OnNewConnection &onc);
  void DisableAccept();
  void EnableAccept();

private:
  void HandleNewConnection(std::unique_ptr<snet::Connection> connection);

  bool enable_accept_;
  OnNewConnection onc_;
  snet::Acceptor acceptor_;
};

class FrontendTcp final {
public:
  FrontendTcp(const std::string &frontend_ip, unsigned short frontend_port,
              const std::string &backend_ip, unsigned short backend_port,
              snet::EventLoop *loop, snet::TimerList *timer_list);

  FrontendTcp(const FrontendTcp &) = delete;
  void operator=(const FrontendTcp &) = delete;

  bool IsListenOk() const;

private:
  void HandleNewConn(std::unique_ptr<Client> conn);
  void HandleConnClose(unsigned long long id);
  void HandleConnData(std::unique_ptr<snet::Buffer> data);

  unsigned long long id_generator_;
  std::unordered_map<unsigned long long, std::unique_ptr<Client>> clients_;
  Frontend frontend_;
  FrontendServer server_;
};

} // namespace forwarder
} // namespace fluorine
