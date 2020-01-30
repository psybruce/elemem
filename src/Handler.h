#ifndef HANDLER_H
#define HANDLER_H

#include "RC/RC.h"
#include "RCqt/Worker.h"
#include "APITests.h"
#include "EDFSave.h"
#include "EEGAcq.h"
#include "StimWorker.h"
#include "StimGUIConfig.h"
#include <QObject>


namespace CML {
  class MainWindow;
  class JSONFile;
  class CSVFile;
  
  class Handler : public RCqt::WorkerThread {
    public:

    Handler();
    ~Handler();

    void SetMainWindow(RC::Ptr<MainWindow> new_main);

    RCqt::TaskCaller<> CerebusTest =
      TaskHandler(Handler::CerebusTest_Handler);

    RCqt::TaskCaller<> CereStimTest =
      TaskHandler(Handler::CereStimTest_Handler);


    RCqt::TaskCaller<const StimSettings> SetStimSettings =
      TaskHandler(Handler::SetStimSettings_Handler);

    RCqt::TaskCaller<> StartExperiment =
        TaskHandler(Handler::StartExperiment_Handler);

    RCqt::TaskCaller<> StopExperiment =
        TaskHandler(Handler::StopExperiment_Handler);

    // Break into vector of handlers
    RCqt::TaskCaller<> TestStim =
      TaskHandler(Handler::TestStim_Handler);

    RCqt::TaskCaller<RC::FileRead> OpenConfig =
      TaskHandler(Handler::OpenConfig_Handler);

    StimWorker stim_worker;
    EEGAcq eeg_acq;
    EDFSave edf_save;
    RC::APtr<const JSONFile> exp_config;
    RC::APtr<const CSVFile> elec_config;

    private:

    RC::Ptr<MainWindow> main_window;

    void CerebusTest_Handler();
    void CereStimTest_Handler();

    void SetStimSettings_Handler(const StimSettings& settings_callback);

    void StartExperiment_Handler() { } // TODO
    void StopExperiment_Handler() { } // TODO

    void TestStim_Handler() { }

    void OpenConfig_Handler(RC::FileRead& fr);

    RC::Data1D<StimSettings> stim_settings;
    RC::Data1D<StimSettings> min_stim_settings;
    RC::Data1D<StimSettings> max_stim_settings;

    bool stim_test_warning = true;
  };
}



#endif // HANDLER_H

