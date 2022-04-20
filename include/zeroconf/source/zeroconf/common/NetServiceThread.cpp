#include "../common/NetServiceThread.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include "src/log.h"
//#include <sys/time.h>

using namespace ZeroConf;

NetServiceThread::NetServiceThread(DNSServiceRef dnsServiceRef, double timeOutInSeconds)
: mDNSServiceRef(dnsServiceRef)
, mTimeOut(timeOutInSeconds)
{
}

NetServiceThread::~NetServiceThread()
{
  if(!waitForThreadToExit(100))
  {
    stopThread(1);
  }
}

bool NetServiceThread::poll(DNSServiceRef dnsServiceRef, double timeOutInSeconds, DNSServiceErrorType &err)
{
	assert(dnsServiceRef);

	err = kDNSServiceErr_NoError;
	
  fd_set readfds;
	FD_ZERO(&readfds);
	
  int dns_sd_fd = DNSServiceRefSockFD(dnsServiceRef);
  int nfds = dns_sd_fd+1;
	FD_SET(dns_sd_fd, &readfds);
	
  struct timeval tv;
  tv.tv_sec = long(floor(timeOutInSeconds));
  tv.tv_usec = long(1000000*(timeOutInSeconds - tv.tv_sec));
	
	int result = select(nfds,&readfds,NULL,NULL,&tv);
	int fddisset = FD_ISSET(dns_sd_fd, &readfds);
	if(result>0 && fddisset)
	{
		err = DNSServiceProcessResult(dnsServiceRef);
		return true;
	}
	
	return false;
}

bool NetServiceThread::poll1(DNSServiceRef dnsServiceRef, double timeOutInSeconds, DNSServiceErrorType &err)
{
    assert(dnsServiceRef);
    err = DNSServiceProcessResult(dnsServiceRef);
    return true;
}

void NetServiceThread::run()
{
	std::cout << "NetServiceThread::start()" << std::endl;
	info("NetServiceThread::start()");
	while (!threadShouldExit()) 
	{
		DNSServiceErrorType err = kDNSServiceErr_NoError;
		if(poll(mDNSServiceRef, mTimeOut, err))
		{
			if(err>0)
			{
				info("NetServiceThread error: {}", err);
				setThreadShouldExit();
			}
		}
	}
	info("NetServiceThread::stop()");
	std::cout << "NetServiceThread::stop()" << std::endl;
}
