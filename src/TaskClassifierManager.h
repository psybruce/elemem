#ifndef CLASSIFICATIONDATA_H
#define CLASSIFICATIONDATA_H

#include "EEGData.h"
#include "RC/Ptr.h"
#include "RC/RStr.h"
#include "RCqt/Worker.h"

namespace CML {
  class Handler;

  using ClassifierCallback = RCqt::TaskCaller<const double>;
  using EEGCallback = RCqt::TaskCaller<RC::APtr<const EEGData>>;

  enum class ClassificationType{
    STIM,
    SHAM,
    NORMALIZE
  };

  class TaskClassifierSettings {
    public:
    ClassificationType cl_type;
    size_t sampling_rate;
    size_t duration_ms;
    size_t binned_sampling_rate;
  };

  // TODO: JPB: Make this a base class
  class TaskClassifierManager : public RCqt::WorkerThread {
    public:
    TaskClassifierManager(RC::Ptr<Handler> hndl, size_t sampling_rate); 

    RCqt::TaskCaller<const ClassificationType, const uint64_t> ProcessClassifierEvent =
      TaskHandler(TaskClassifierManager::ProcessClassifierEvent_Handler);

    ClassifierCallback ClassifierDecision =
      TaskHandler(TaskClassifierManager::ClassifierDecision_Handler);

    RCqt::TaskCaller<const EEGCallback> SetCallback =
      TaskHandler(TaskClassifierManager::SetCallback_Handler);

    protected:
    RCqt::TaskCaller<RC::APtr<const EEGData>> ClassifyData = 
      TaskHandler(TaskClassifierManager::ClassifyData_Handler);

    // TODO: Decide whether to have json configurable variables for the
    //       classifier data, such as binning sizes

    //void StartClassifier_Handler(const RC::RStr& filename,
    //                             const FullConf& conf) override;
    // Thread ordering constraint:
    // Must call Stop after Start, before this Destructor, and before
    // hndl->eeg_acq is deleted.
    //void StopClassifier_Handler() override;
    void ClassifyData_Handler(RC::APtr<const EEGData>& data);

    void ProcessClassifierEvent_Handler(const ClassificationType& cl_type, const uint64_t& duration_ms);
    void ClassifierDecision_Handler(const double& result);
    
    void SetCallback_Handler(const EEGCallback& new_callback);

    RC::APtr<EEGData> GetCircularBufferData();
    void PrintCircularBuffer();
    void UpdateCircularBuffer(RC::APtr<const EEGData>& new_data);
    void UpdateCircularBuffer(RC::APtr<const EEGData>& new_data, size_t start);
    void UpdateCircularBuffer(RC::APtr<const EEGData>& new_data, size_t start, size_t amnt);
    RC::APtr<EEGData> BinData(RC::APtr<const EEGData> in_data, size_t new_sampling_rate);

    void StartClassification();

    RC::Ptr<Handler> hndl;
    RC::RStr callback_ID;

    EEGData circular_data;
    size_t circular_data_start = 0;

    TaskClassifierSettings task_classifier_settings;
    size_t sampling_rate;

    bool stim_event_waiting = false;
    size_t num_eeg_events_before_stim = 0;

    EEGCallback callback;
  };
}

#endif // CLASSIFICATIONDATA_H
