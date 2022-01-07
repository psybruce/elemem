#include "RC/Macros.h"
#include "Classifier.h"
#include "EEGAcq.h"
#include "Handler.h"
#include "JSONLines.h"
#include "TaskClassifierManager.h"
#include <unordered_map>

namespace CML {
  TaskClassifierManager::TaskClassifierManager(RC::Ptr<Handler> hndl,
      size_t sampling_rate, size_t bin_frequency)
      : hndl(hndl), circular_data(0), sampling_rate(sampling_rate) {
    callback_ID = RC::RStr("TaskClassifierManager_") + bin_frequency;
    task_classifier_settings.binned_sampling_rate = bin_frequency;

    hndl->eeg_acq.RegisterCallback(callback_ID, ClassifyData);

    // Test Code for binning
    //RC::APtr<EEGData> data = new EEGData(10);
    //data->data.Resize(1);
    //data->data[0].Append({0,1,2,3,4,5,6,7,8,9,10});
    //RC::APtr<const EEGData> binned_data = BinData(data.ExtractConst(), 3).ExtractConst();
    //PrintEEGData(*binned_data);
  }

  void TaskClassifierManager::UpdateCircularBuffer(RC::APtr<const EEGData>& new_data) {
    size_t start = 0;
    size_t amnt = new_data->data[0].size();
    UpdateCircularBuffer(new_data, start, amnt);
  }

  void TaskClassifierManager::UpdateCircularBuffer(RC::APtr<const EEGData>& new_data, size_t start) {
    size_t amnt = new_data->data[0].size() - start;
    UpdateCircularBuffer(new_data, start, amnt);
  }

  void TaskClassifierManager::UpdateCircularBuffer(RC::APtr<const EEGData>& new_data, size_t start, size_t amnt) {
    auto& new_datar = new_data->data;
    auto& circ_datar = circular_data.data;

    // TODO: JPB: (refactor) Decide if this is how I should set the sampling_rate and data size in UpdateCircularBuffer 
    //                       (or should I pass all the info in the constructor)
    // Setup the circular EEGData to match the incoming EEGData
    if (circular_data.sampling_rate == 0) {
      circular_data.sampling_rate = new_data->sampling_rate;
      circ_datar.Resize(new_datar.size());
      RC_ForIndex(i, circ_datar) { // Iterate over channels
        // TODO: JPB: (need) Make the circular buffer size configurable
        circ_datar[i].Resize(100);
      }
    }

    if (new_datar.size() > circ_datar.size())
      Throw_RC_Type(Bounds, "The number of channels in new_data and circular_data do not match");
    if (start > new_datar.size() - 1)
      Throw_RC_Type(Bounds, "The \"start\" value is larger than the number of items that new_data contains");
    if (start + amnt > new_datar[0].size())
      Throw_RC_Type(Bounds, "The end value is larger than the number of items that new_data contains");
    // TODO: JPB: (feature) Log error message and write only the last buffer length of data
    if (new_datar[0].size() > circ_datar[0].size())
      Throw_RC_Type(Bounds, "Trying to write more values into the circular_data than the circular_data contains");

    if (amnt ==  0) { return; } // Not writing any data, so skip

    RC_ForIndex(i, circ_datar) { // Iterate over channels
      auto& circ_events = circ_datar[i];
      auto& new_events = new_datar[i];
      size_t circ_remaining_events = circ_events.size() - circular_data_start;

      if (new_events.IsEmpty()) { continue; }

      // Copy the data up to the end of the Data1D (or all the data, if possible)
      size_t frst_amnt = std::min(circ_remaining_events, amnt);
      circ_events.CopyAt(circular_data_start, new_events, start, frst_amnt);
      circular_data_start += frst_amnt;
      if (circular_data_start == circ_events.size())
        circular_data_start = 0;

      // Copy the remaining data at the beginning of the Data1D
      int scnd_amnt = (int)amnt - (int)frst_amnt;
      if (scnd_amnt > 0) {
        circ_events.CopyAt(0, new_events, start+frst_amnt, scnd_amnt);
        circular_data_start += scnd_amnt;
      }
    }
  }

  void TaskClassifierManager::PrintCircularBuffer() {
    RC_DEBOUT(RC::RStr("circular_data_start: ") + circular_data_start + "\n");;
    auto data = GetCircularBufferData();
    PrintEEGData(*data);
  }

  RC::APtr<EEGData> TaskClassifierManager::GetCircularBufferData() {
    RC::APtr<EEGData> out_data = new EEGData(circular_data.sampling_rate);
    auto& circ_datar = circular_data.data;
    auto& out_datar = out_data->data;
    out_datar.Resize(circ_datar.size());
    RC_ForIndex(i, circ_datar) { // Iterate over channels
      auto& circ_events = circ_datar[i];
      auto& out_events = out_datar[i];
      out_events.Resize(circ_events.size());
      size_t amnt = circ_events.size() - circular_data_start;
      out_events.CopyAt(0, circ_events, circular_data_start, amnt);
      out_events.CopyAt(amnt, circ_events, 0, circular_data_start);
    }
    return out_data;
  }

  RC::APtr<EEGData> TaskClassifierManager::BinData(RC::APtr<const EEGData> in_data, size_t new_sampling_rate) {
    // TODO: JPB: (feature) Add ability to handle sampling ratios that aren't true multiples
    RC::APtr<EEGData> out_data = new EEGData(new_sampling_rate);
    size_t sampling_ratio = in_data->sampling_rate / new_sampling_rate;
    auto& in_datar = in_data->data;
    auto& out_datar = out_data->data;
    out_datar.Resize(in_datar.size());
    
    auto accum_event = [](u32 sum, size_t val) { return std::move(sum) + val; };
    RC_ForIndex(i, out_datar) { // Iterate over channels
      auto& in_events = in_datar[i];
      auto& out_events = out_datar[i];

      if (in_events.IsEmpty()) { continue; }
      size_t out_events_size = in_events.size() / sampling_ratio + 1;
      out_events.Resize(out_events_size);
      RC_ForIndex(j, out_events) {
        if (j < out_events_size - 1) { 
          size_t start = j * sampling_ratio;
          size_t end = (j+1) * sampling_ratio - 1;
          size_t items = sampling_ratio;
          out_events[j] = std::accumulate(&in_events[start], &in_events[end]+1, 0, accum_event) / items;
        } else { // Last block could have leftover samples
          size_t start = j * sampling_ratio;
          size_t end = in_events.size() - 1;
          size_t items = std::distance(&in_events[start], &in_events[end]+1);
          RC_DEBOUT(items);
          out_events[j] = std::accumulate(&in_events[start], &in_events[end]+1, 0, accum_event) / items;
        }
      }
    }

    return out_data;
  }

  void TaskClassifierManager::StartClassification() {
    stim_event_waiting = false;
    num_eeg_events_before_stim = 0;

    RC::APtr<const EEGData> data = GetCircularBufferData().ExtractConst();
    RC::APtr<const EEGData> binned_data = BinData(data, task_classifier_settings.binned_sampling_rate).ExtractConst();

    // TODO: JPB: (need) Handle Normalization with RollingStats

    callback(binned_data, task_classifier_settings);
  }

  void TaskClassifierManager::ClassifyData_Handler(RC::APtr<const EEGData>& data) {
    //RC_DEBOUT(RC::RStr("TaskClassifierManager_Handler\n"));
    auto& datar = data->data;

    if (stim_event_waiting) {
      if (num_eeg_events_before_stim <= datar.size()) {
        UpdateCircularBuffer(data, 0, num_eeg_events_before_stim);
        StartClassification();
        UpdateCircularBuffer(data, num_eeg_events_before_stim);
      } else { // num_eeg_events_before_stim > datar.size()
        UpdateCircularBuffer(data);
        num_eeg_events_before_stim -= datar.size();
      }
    } else {
      // TODO: JPB: (feature) This can likely be removed to reduce overhead
      //            If there is no stim event waiting, then don't update data
      UpdateCircularBuffer(data);
    }
  }

  void TaskClassifierManager::ProcessClassifierEvent_Handler(
        const ClassificationType& cl_type, const uint64_t& duration_ms,
        const uint64_t& classif_id) {
    if (!stim_event_waiting) {
      stim_event_waiting = true;
      num_eeg_events_before_stim = duration_ms * sampling_rate / 1000;
      task_classifier_settings.cl_type = cl_type;
      task_classifier_settings.duration_ms = duration_ms;
      task_classifier_settings.classif_id = classif_id;
    } else {
      // TODO: JPB: (feature) Allow classifier to start gather EEGData at the same time as another gathering?  Requires id queue.
      hndl->event_log.Log("Skipping stim event, another stim event is already waiting (collecting EEGData)");
    }
  }

  void TaskClassifierManager::ClassifierDecision_Handler(const double& result,
      const TaskClassifierSettings& task_classifier_settings) {
    //RC_DEBOUT(RC::RStr("ClassifierDecision_Handler\n\n"));
    bool stim = result < 0.5;
    bool stim_type =
      (task_classifier_settings.cl_type == ClassificationType::STIM);

    JSONFile data;
    data.Set(result, "result");
    data.Set(stim, "decision");

    RC::RStr type;
    switch (task_classifier_settings.cl_type) {
      case ClassificationType::STIM: type = "STIM_DECISON"; break;
      case ClassificationType::SHAM: type = "SHAM_DECISON"; break;
      default: Throw_RC_Error("Invalid classification type received.");
    }

    auto resp = MakeResp(type, task_classifier_settings.classif_id, data);
    hndl->event_log.Log(resp.Line());

    if (stim_type && stim) {
      // TODO: JPB: (need) Temporarily remove call to stimulate
      //hndl->stim_worker.Stimulate();
    }
  }

  void TaskClassifierManager::SetCallback_Handler(const TaskClassifierCallback& new_callback) {
    callback = new_callback;
  }
}
