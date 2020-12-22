#include "StimWorker.h"
#include "ConfigFile.h"
#include "EventLog.h"
#include "Handler.h"
#include "JSONLines.h"
#include "NetWorker.h"
#include "StatusPanel.h"

namespace CML {
  StimWorker::StimWorker(RC::Ptr<Handler> hndl)
    : hndl(hndl) {
  }


  void StimWorker::ConfigureStimulation_Handler(const CSStimProfile& profile) {
    cur_profile = profile;
    cerestim.ConfigureStimulation(profile);

    max_duration = 0;
    for (size_t i=0; i<profile.size(); i++) {
      max_duration = std::max(max_duration, profile[i].duration);
    }
  }


  void StimWorker::Stimulate_Handler() {
    cerestim.Stimulate();
    status_panel->SetStimming(max_duration);

    JSONFile event_base = MakeResp("STIMMING");
    for (size_t i=0; i<cur_profile.size(); i++) {
      JSONFile event = event_base;
      event.Set(uint32_t(cur_profile[i].electrode_pos), "data",
          "electrode_pos");
      event.Set(uint32_t(cur_profile[i].electrode_neg), "data",
          "electrode_neg");
      event.Set(cur_profile[i].amplitude*1e-3, "data", "amplitude");
      event.Set(cur_profile[i].frequency, "data", "frequency");
      event.Set(cur_profile[i].duration*1e-3, "data", "duration");

      hndl->event_log.Log(event.Line());
    }
  }

  void StimWorker::CloseCereStim_Handler() {
    cerestim.Close();
  }
}

