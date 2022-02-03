#include "include/oscpkt/oscpkt.hh"
#include "include/oscpkt/udp.hh"

// #include <stdio.h>

class OSCManager {

  void handle_receive() {
    if (m_recv_sock.receiveNextPacket(0)) {
      oscpkt::PacketReader reader(m_recv_sock.packetData(), m_recv_sock.packetSize());
      oscpkt::Message *msg;
      while (reader.isOk() && (msg = reader.popMessage()) != 0) {
        // cout << "Client: received " << *msg << "\n";
      }
    }
  }


private:

  bool _send(const oscpkt::Message& msg){
    oscpkt::PacketWriter writer;
    writer.init().addMessage(msg);
    return m_send_sock.sendPacket(writer.packetData(), writer.packetSize());
  }

  oscpkt::UdpSocket m_send_sock;
  oscpkt::UdpSocket m_recv_sock;
};