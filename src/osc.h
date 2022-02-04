#include "include/oscpkt/oscpkt.hh"
#include "include/oscpkt/udp.hh"

#include <stdio.h>
#include <map>

using OSCCallback = std::function<void(oscpkt::Message&)>;

class OSCManager {

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
        // try to pattern match
        for (const auto &pair : m_patterns){
          // if we have a match, call the callback
          if (msg->match(pair.first))
            pair.second(*msg);
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

  void add_callback(std::string pattern, OSCCallback callback) {
    m_patterns[pattern] = callback;
  }

private:
  std::map<std::string, OSCCallback> m_patterns;

  std::string m_addr {"localhost"} ;
  int m_send_port {0};
  int m_recv_port {0};
  bool m_init {false};

  oscpkt::UdpSocket m_send_sock;
  oscpkt::UdpSocket m_recv_sock;
};