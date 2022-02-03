#include "include/oscpkt/oscpkt.hh"
#include "include/oscpkt/udp.hh"

class OSCManager {

  OSCManager(std::string &addr, int send_port, int recv_port) {
    m_send_sock.connectTo(addr, send_port);
    m_recv_sock.bindTo(recv_port);
  }

  void handle_receive() {
    if (m_recv_sock.receiveNextPacket(0)) {
      oscpkt::PacketReader reader(m_recv_sock.packetData(), m_recv_sock.packetSize());
      oscpkt::Message *msg;
      while (reader.isOk() && (msg = reader.popMessage()) != 0) {
        // cout << "Client: received " << *msg << "\n";
      }
    }
  }

  bool send(const oscpkt::Message& msg) {
    oscpkt::PacketWriter writer;
    writer.init().addMessage(msg);
    return m_send_sock.sendPacket(writer.packetData(), writer.packetSize());
  }

private:
  oscpkt::UdpSocket m_send_sock;
  oscpkt::UdpSocket m_recv_sock;
};