#include "include/oscpkt/oscpkt.hh"
#include "include/oscpkt/udp.hh"

#include <stdio.h>
#include <map>

// return true if you successfully handled the message
// and popped all the arguments
using OSCCallback = std::function<bool(oscpkt::Message&)>;

class OSCManager {
  OSCManager();

public:
  OSCManager(std::string &addr, int send_port, int recv_port)
    : m_addr(addr), m_send_port(send_port), m_recv_port(recv_port) {}

  bool init() {
    m_init = m_send_sock.connectTo(m_addr, m_send_port) \
              && m_recv_sock.bindTo(m_recv_port);
    return m_init;
  }

  void handle_receive(bool block = false) {
    if (!m_init) {return;}

    if (m_recv_sock.receiveNextPacket(block ? -1 : 0)) {
      // setup a reader
      oscpkt::PacketReader reader(m_recv_sock.packetData(), m_recv_sock.packetSize());
      oscpkt::Message *msg {nullptr};

      // pop as many messages as are in the packet
      while (reader.isOk() && (msg = reader.popMessage()) != 0) {
        for (const auto &callback : m_callbacks){
          // if the callback matched and processed the message, break
          if(callback(*msg))
            break;
        }
      }
    }
  }

  bool send(const oscpkt::Message& msg) {
    if (!m_init) {return false;}

    oscpkt::PacketWriter writer;
    writer.init().addMessage(msg);
    return m_send_sock.sendPacket(writer.packetData(), writer.packetSize());
  }

  void add_callback(OSCCallback callback) {
    m_callbacks.emplace_back(callback);
  }

private:
  std::vector<OSCCallback> m_callbacks;

  std::string m_addr {"localhost"} ;
  int m_send_port {0};
  int m_recv_port {0};
  bool m_init {false};

  oscpkt::UdpSocket m_send_sock;
  oscpkt::UdpSocket m_recv_sock;
};