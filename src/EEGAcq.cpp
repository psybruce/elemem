#include "EEGAcq.h"
#include "RC/RTime.h"
#include "RC/Errors.h"
#include "Cerebus.h" // TODO Remove after moving injection to Handler.
#include "CerebusSim.h" // TODO Remove after moving injection to Handler.


namespace CML {
  EEGAcq::EEGAcq() {
    AddToThread(this);
  }

  EEGAcq::~EEGAcq() {
    StopEverything();
  }


  // TODO This needs to split data by channel type.
  // First split SetChannels into SetMacroChannels/SetMicroChannels.
  void EEGAcq::GetData_Slot() {
    if (ShouldAbort()) {
      StopEverything();
      return;
    }

    if (eeg_source.IsNull() || !channels_initialized) {
      return;
    }

    try {
      RC::APtr<EEGData> data_aptr = new EEGData(sampling_rate);
      auto& data = data_aptr->data;
      data.Resize(cbNUM_ANALOG_CHANS);

      auto& cereb_chandata = eeg_source->GetData();

      size_t max_len = 0;
      for (size_t c=0; c<cereb_chandata.size(); c++) {
        max_len = std::max(max_len, cereb_chandata[c].data.size());
      }

      if (max_len == 0) {
        return;
      }

      for(size_t i=0; i<cereb_chandata.size(); i++) {
        auto cereb_chan = cereb_chandata[i].chan;
        if (cereb_chan >= data.size()) {
          continue;
        }
        auto& cereb_data = cereb_chandata[i].data;
        auto& chan = data[cereb_chan];
        chan.Resize(max_len);
        for (size_t d=0; d<cereb_data.size(); d++) {
          chan[d] = cereb_data[d];
        }
        // Fill in zeros if needed to guarantee all channels the same size.
        for (size_t d=cereb_data.size(); d<chan.size(); d++) {
          chan[d] = 0;
        }
      }


      auto data_captr = data_aptr.ExtractConst();
      for (size_t i=0; i<data_callbacks.size(); i++) {
        data_callbacks[i].callback(data_captr);
      }
    }
    catch (...) {
      // Stop acquisition timer upon error, and one pop-up only.
      StopEverything();
      throw;
    }
  }


  void EEGAcq::SetSource_Handler(RC::APtr<EEGSource>& new_source) {
    eeg_source = new_source;
  }


  void EEGAcq::InitializeChannels_Handler(const size_t& new_sampling_rate) {
    if (eeg_source.IsNull()) {
      return;
    }

    sampling_rate = new_sampling_rate;

    StopEverything();

    eeg_source->InitializeChannels(sampling_rate);

    channels_initialized = true;
    BePollingIfCallbacks();
  }


  void EEGAcq::RegisterCallback_Handler(const RC::RStr& tag,
                                        const EEGCallback& callback) {
    RemoveCallback_Handler(tag);

    data_callbacks += TaggedCallback{tag, callback};

    BePollingIfCallbacks();
  }


  void EEGAcq::RemoveCallback_Handler(const RC::RStr& tag) {
    for (size_t i=0; i<data_callbacks.size(); i++) {
      if (data_callbacks[i].tag == tag) {
        data_callbacks.Remove(i);
        i--;
      }
    }

    if (data_callbacks.size() == 0 && acq_timer.IsSet()) {
      acq_timer->stop();
    }
  }


  void EEGAcq::CloseSource_Handler() {
    if (eeg_source.IsSet()) {
      eeg_source->Close();
    }
    channels_initialized = false;
    StopEverything();
  }


  void EEGAcq::StopEverything() {
    if (acq_timer.IsSet()) {
      acq_timer->stop();
      acq_timer.Delete();
    }
  }


  void EEGAcq::BeAllocatedTimer() {
    if (acq_timer.IsNull()) {
      acq_timer = new QTimer();
      acq_timer->setTimerType(Qt::PreciseTimer);

      QObject::connect(acq_timer.Raw(), &QTimer::timeout, this,
                     &EEGAcq::GetData_Slot);
    }
  }


  void EEGAcq::BePollingIfCallbacks() {
    if (DirectCallingMode()) {
      return;
    }
    BeAllocatedTimer();
    if (!acq_timer->isActive() && data_callbacks.size() > 0) {
      acq_timer->start(polling_interval_ms);
    }
  }
}

