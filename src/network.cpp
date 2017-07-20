#include "network.h"

#include <memory>
#include <sstream>

// Included for MSG_PEEK symbol not present in Poco
#include <sys/socket.h>

using namespace std::string_literals;  // NOLINT (google-build-using-namespace)

namespace franka {

Network::Network(const std::string& franka_address,
                 uint16_t franka_port,
                 std::chrono::milliseconds tcp_timeout,
                 std::chrono::milliseconds udp_timeout) {
  try {
    Poco::Timespan poco_timeout(1000l * tcp_timeout.count());
    tcp_socket_.connect({franka_address, franka_port}, poco_timeout);
    tcp_socket_.setBlocking(true);
    tcp_socket_.setSendTimeout(poco_timeout);
    tcp_socket_.setReceiveTimeout(poco_timeout);

    // Activate TCP keepalive
    tcp_socket_.setKeepAlive(true);
    // After an idle time of 1 sec a keepalive probe is sent
    tcp_socket_.setOption(IPPROTO_TCP, TCP_KEEPIDLE, 1);
    // After 3 keepalive probes the connection is closed
    tcp_socket_.setOption(IPPROTO_TCP, TCP_KEEPCNT, 3);
    // The time between the keepalive probes is set to 1 sec
    tcp_socket_.setOption(IPPROTO_TCP, TCP_KEEPINTVL, 1);

    udp_socket_.setReceiveTimeout(Poco::Timespan{1000l * udp_timeout.count()});
    udp_socket_.bind({"0.0.0.0", 0});
  } catch (const Poco::Net::NetException& e) {
    throw NetworkException("libfranka: FRANKA connection error: "s + e.what());
  } catch (const Poco::TimeoutException& e) {
    throw NetworkException("libfranka: FRANKA connection timeout"s);
  } catch (const Poco::Exception& e) {
    throw NetworkException("libfranka: "s + e.what());
  }
}

Network::~Network() {
  try {
    tcp_socket_.shutdown();
  } catch (...) {
  }
}

uint16_t Network::udpPort() const noexcept {
  return udp_socket_.address().port();
}

void Network::checkTcpConnection() try {
  if (tcp_socket_.poll(0, Poco::Net::Socket::SELECT_READ)) {
    std::array<uint8_t, 1> buffer;
    int rv = tcp_socket_.receiveBytes(buffer.data(), static_cast<int>(buffer.size()), MSG_PEEK);

    if (rv == 0) {
      throw NetworkException("libfranka: server closed connection");
    }
  }
} catch (const Poco::Exception& e) {
  throw NetworkException("libfranka: "s + e.what());
}

void Network::tcpReceiveIntoBuffer(uint8_t* buffer, size_t read_size) {
  size_t bytes_read = 0;
  try {
    while (bytes_read < read_size) {
      size_t bytes_left = read_size - bytes_read;
      int rv = tcp_socket_.receiveBytes(&buffer[bytes_read], static_cast<int>(bytes_left));
      if (rv < 0) {
        throw NetworkException("libfranka: server closed connection");
      }
      bytes_read += rv;
    }
  } catch (const Poco::TimeoutException& e) {
    if (bytes_read != 0) {
      throw ProtocolException("libfranka: incorrect object size");
    } else {
      throw NetworkException("libfranka: FRANKA connection timeout");
    }
  } catch (const Poco::Exception& e) {
    throw NetworkException("libfranka: FRANKA connection closed");
  }
}

int Network::udpAvailableData() {
  return udp_socket_.available();
}

}  // namespace franka
