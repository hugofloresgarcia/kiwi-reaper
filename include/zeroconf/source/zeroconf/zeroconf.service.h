/**
 */
#pragma once

#include "common/VERSION.h"		
#include "common/NetService.h"
#include "common/NetServiceThread.h"

#include <iostream>

using namespace ZeroConf;

class Service : public NetService, public NetServiceListener
{
public:
  Service(const std::string &domain, 
          const std::string &type, 
          const std::string &name, 
          const int port)
  : NetService(domain, type, name, port)
  {
    setListener(this);
  }
  
private:
  virtual void willPublish(NetService *pNetService) { info ("will publish service"); }
  virtual void didNotPublish(NetService *pNetService) { 
    info("Service was unable to publish");
  }
  virtual void didPublish(NetService *pNetService)
  {
    info("Service published: {} {}", pNetService->getName().c_str(), pNetService->getPort());
  }
  
  virtual void willResolve(NetService *pNetService) {
    info("will attempt to resolve service: {}, {}", pNetService->getName(), pNetService->getPort());
  }
  virtual void didNotResolve(NetService *pNetService) {
    info("failed to resolve service: {}, {}", pNetService->getName(), pNetService->getPort());
  }
  virtual void didResolveAddress(NetService *pNetService) {
    info("successful resolve service: {}, {}", pNetService->getName(), pNetService->getPort());
  }
  virtual void didUpdateTXTRecordData(NetService *pNetService) {}   
  virtual void didStop(NetService *pNetService) {
    info("service did stop", pNetService->getName(), pNetService->getPort());
  }
};


class zeroconf_service 
{
public:
  zeroconf_service(const std::string &domain, 
                   const std::string &type, 
                   const std::string &name, 
                   const int port) {
    m_pService = std::make_unique<Service>(domain, type,
                                           name, port);
    m_pService->publishWithOptions(NetService::Options(1), true);
  };

  void poll() 
  {	
    // poll for results
    if(m_pService && m_pService->getDNSServiceRef())
    {
      DNSServiceErrorType err = kDNSServiceErr_NoError;
      
      if(NetServiceThread::poll(m_pService->getDNSServiceRef(), 10, err))
      {
        if(err > 0)
        {
          m_pService->stop();
          info("error {}", err);
        }
        else {
          info("polled message with no error");
          
        }
      }
      else {
        info("no response");
      }
    } 
    else {
      info("invalid service or service ref");
    }
  }


private:
  long port;
  std::unique_ptr<Service> m_pService {nullptr};
};
