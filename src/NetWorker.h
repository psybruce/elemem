#ifndef NETWORKER_H
#define NETWORKER_H

#include "RC/APtr.h"
#include "RC/RStr.h"
#include "RC/Ptr.h"
#include "RCqt/Worker.h"
#include "ConfigFile.h"
#include <QTcpServer>
#include <QTcpSocket>


namespace CML {
  class Handler;

  class NetWorker : public RCqt::WorkerThread, public QObject {
    public:

    NetWorker(RC::Ptr<Handler> hndl, const RC::RStr& netWorkerType);
    ~NetWorker();

    // Rule of 3.
    NetWorker(const NetWorker&) = delete;
    NetWorker& operator=(const NetWorker&) = delete;

    RCqt::TaskCaller<const RC::RStr, const uint16_t> Listen =
      TaskHandler(NetWorker::Listen_Handler);

    RCqt::TaskBlocker<> Close =
      TaskHandler(NetWorker::Close_Handler);

    RCqt::TaskGetter<bool> IsConnected =
      TaskHandler(NetWorker::IsConnected_Handler);

    RCqt::TaskCaller<const bool> WarnOnDisconnect =
      TaskHandler(NetWorker::WarnOnDisconnect_Handler);

    protected slots:
    virtual void NewConnection();
    virtual void DataReady();
    virtual void Disconnected();

    protected:
    void Listen_Handler(const RC::RStr& address, const uint16_t& port);
    void Close_Handler();

    bool IsConnected_Handler();
    void WarnOnDisconnect_Handler(const bool& warn);

    virtual void ProcessCommand(RC::RStr cmd) = 0;
    void Respond(JSONFile& resp);


    RC::Ptr<Handler> hndl;
    RC::APtr<QTcpServer> server;
    RC::APtr<QTcpSocket> con;
    RC::RStr buffer;
    bool stop_on_disconnect = false;
    bool configured = false;
    bool connected = false;
#ifdef NETWORKER_TIMING
    RC::Time timer;
#endif // NETWORKER_TIMING
    
    RC::RStr netWorkerType = "";
  };
}

#endif // NETWORKER_H

