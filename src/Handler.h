#ifndef HANDLER_H
#define HANDLER_H

#include "RC/RC.h"
#include "RCqt/Worker.h"
#include "APITests.h"
#include "EEGAcq.h"
#include "StimWorker.h"
#include <QObject>


namespace CML {
  class MainWindow;
  
  class Handler : public RCqt::WorkerThread {
    public:

    Handler();

    void SetMainWindow(RC::Ptr<MainWindow> new_main);

    RCqt::TaskCaller<> CerebusTest =
      TaskHandler(Handler::CerebusTest_Handler);

    RCqt::TaskCaller<> CereStimTest =
      TaskHandler(Handler::CereStimTest_Handler);

    EEGAcq eeg_acq;
    StimWorker stim_worker;

    RCqt::TaskCaller<const f64> TestLabel =
      TaskHandler(Handler::TestLabel_Handler);

    // Break into vector of handlers
    RCqt::TaskCaller<> TestStim =
      TaskHandler(Handler::TestStim_Handler);

    private:

    RC::Ptr<MainWindow> main_window;

    void CerebusTest_Handler();
    void CereStimTest_Handler();

    void TestLabel_Handler(const f64& x) { std::cout << x << std::endl; }
    void TestStim_Handler() { }

    bool stim_test_warning = true;
  };
}



#endif // HANDLER_H

