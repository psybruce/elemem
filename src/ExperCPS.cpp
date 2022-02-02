#include "ExperCPS.h"
#include "Handler.h"
#include "JSONLines.h"
#include "Popup.h"
#include "StatusPanel.h"
#include "RC/Data1D.h"

using namespace RC;

// TODO change name from CPS to something less easily mistaken for OPS

namespace CML {
  ExperCPS::ExperCPS(RC::Ptr<Handler> hndl)
    : hndl(hndl) {
    AddToThread(this);

    // cnpy::npz_t npz_dict;
    // CKern* k = getSklearnKernel((unsigned int)x_dim, npz_dict, kernel, std::string(""), true);
    k = new CMatern32Kern(x_dim);
    kern = CCmpndKern{x_dim};
    kern.addKern(k);
    whitek = new CWhiteKern(x_dim);
    kern.addKern(whitek);

    // TODO load parameters from config file - ask Ryan/James about how this is handled
    n_normalize_events = 25;
    poststim_classif_lockout_ms = 500;
    stim_lockout_ms = 2500;
    normalize_lockout_ms = 3000; // should be stim_lockout_ms + stim duration, though duration may not be fixed
    // TODO: JPB: move to config file for setting classifier settings during initialization?
    // classification interval duration currently
    classify_ms = 1350;

    // classifier event counter
    classif_id = 0;

    seed = 1;
    n_var = 1;
    obsNoise = 0.1;
    exp_bias = 0.25;
    n_init_samples = 100;

    BayesianSearchModel BO(kern, &test.x_interval, obsNoise * obsNoise, exp_bias, n_init_samples, seed, verbosity);
    BayesianSearchModel search();
  }

  ExperCPS::~ExperCPS() {
    delete k;
    delete whitek;
    Stop_Handler();
  }

  void ExperCPS::SetStimProfiles_Handler(
        const RC::Data1D<CSStimProfile>& new_stim_loc_profiles) {
    stim_loc_profiles = new_stim_loc_profiles;
  }

  void ExperCPS::GetNextEvent() {
    CMatrix* stim_pars = search.get_next_sample();

    // TODO: RDD: update to select between multiple stim sites
    // TODO: all: currently assuming that stim parameters not being searched over are set separately in
    //            stim_loc_profiles, should ensure these are set in Settings.cpp
    CSStimChannel stim_info(stim_loc_profiles[0][0]);

    // set stim parameter values for next search point
    // TODO: RDD: may want to scale down into more desirable interval, e.g., [0, 1]

    // convert to allowable discrete stim settings
    // stim_pars amplitude in mA
    // CSStimChannel.amplitude in uA but allowed stim values in increments of 100 uA
    // TODO: RDD: check this conversion works
    stim_info.amplitude = ((uint16_t)(stim_pars.vals(0)*10 + 0.5)) * 100;

    stim_profiles += stim_info;

    ExpEvent ev;
    ev.active_ms = stim_info.duration/1000;
    // TODO: RDD: reevaluate ISI values
    // Add ISI
    ev.event_ms = ev.active_ms +
      rng.GetRange(cps_specs.intertrial_range_ms[0],
          cps_specs.intertrial_range_ms[1]);

    // auto prefunc = MakeFunctor<void>(
    //     hndl->stim_worker.ConfigureStimulation.ToCaller().
    //     Bind(stim_profiles[p]));
    // ev.pre_event =
    //   MakeCaller(this, &ExperCPS::DoConfigEvent).Bind(prefunc);

    // ev.event = MakeFunctor<void>(
    //     MakeCaller(this, &ExperCPS::DoStimEvent).Bind(
    //     hndl->stim_worker.Stimulate.ToCaller()));

    exp_events += ev;
  }

  void UpdateSearch(const CSStimChannel stim_info, const ExpEvent ev, const double biomarker) {
    // ExpEvent ev currently unused; API allows for future experiments in which stim duration is tuned
    CMatrix stim_pars(1, 1);
    // map stim parameters for search model
    // TODO: RDD: remove magic numbers for conversion factors, 
    //            easy to forget to change in both UpdateSearch() and GetNextEvent()
    stim_pars(0) = ((double)stim_info.amplitude)/1000;

    CMatrix biomarker_cmat(biomarker);
    search.add_sample(stim_pars, biomarker_mat);
  }


  // TODO: RDD/JPB: classifier handler is calling stim events. Are there any needs for these events/status panel messages to be managed in ExperCPS.cpp?
  void ExperCPS::DoConfigEvent(RC::Caller<>event) {
    status_panel->SetEvent("PRESET");
    event();
  }


  // TODO: RDD/JPB: classifier handler is calling stim events. Are there any needs for these events/status panel messages to be managed in ExperCPS.cpp?
  void ExperCPS::DoStimEvent(RC::Caller<> event) {
    status_panel->SetEvent("STIM");
    event();
  }


  // TODO: RDD/JPB: classifier handler is calling stim events. Are there any needs for these events/status panel messages to be managed in ExperCPS.cpp?
  void ExperCPS::DoShamEvent() {
    JSONFile sham_event = MakeResp("SHAM");
    sham_event.Set(cps_specs.sham_duration_ms, "data", "duration");
    hndl->event_log.Log(sham_event.Line());
    status_panel->SetEvent("SHAM");
  }


  void ExperCPS::Start_Handler() {
    cur_ev = 0;
    event_time = 0;
    next_min_event_time = 0;

    // Total run time (ms), fixes experiment length for CPS.
    experiment_duration = 7200 * 1000;

    // Confirm window for run time.
    if (!ConfirmWin(RC::RStr("Total run time will be ") + experiment_duration / (60 * 1000) + " min. "
          + (experiment_duration * 1000) % 60 + " sec.", "Session Duration")) {
      hndl->StopExperiment();
      return;
    }

    exp_start = RC::Time::Get();

    JSONFile startlog = MakeResp("START");
    hndl->event_log.Log(startlog.Line());

    GetNextEvent();
    hndl->task_classifier_manager->ProcessClassifierEvent(
        ClassificationType::NORMALIZE, classify_ms, 0);
  }


  void ExperCPS::Stop_Handler() {
    if (timer.IsSet()) {
      timer->stop();
    }

    JSONFile stoplog = MakeResp("EXIT");
    hndl->event_log.Log(stoplog.Line());
  }


  void ExperCPS::InternalStop() {
    Stop_Handler();
    hndl->StopExperiment();
  }


  void ExperCPS::WaitUntil(uint64_t target_ms) {
    // delays until target_ms milliseconds from the start of the experiment
    // breaks delay every 50 ms to allow for stopping the experiment
    f64 cur_time = RC::Time::Get();
    uint64_t current_time_ms = uint64_t(1000*(cur_time - exp_start)+0.5);

    event_time = target_ms;

    uint64_t delay;
    if (target_ms <= current_time_ms) {
      delay = 0;
    } else {
      delay = target_ms - current_time_ms;
    }

    // delay in 50 ms increments to ensure thread sensitivity to shutdowns
    // TODO test delay accuracy
    // TODO implement me (min)
    uint64_t delay_next = min(delay, 50);
    while (delay > 0) {
      delay_next = min(delay, 50);
      delay -= delay_next;
      QThread::msleep(delay_next);
    }
  }

  // TODO: RDD/JPB: update callback signature
  void ExperCPS::ClassifierDecision_Handler(const double& result,
        const bool& stim_event,
        const TaskClassifierSettings& task_classifier_settings,
        const CSStimChannel& stim_params) {

    f64 cur_time_sec = RC::Time::Get();
    uint64_t cur_time_ms = uint64_t(1000*(cur_time_sec - exp_start)+0.5);
    // TODO: RDD: ensure that last events in all saved event arrays can be disambiguated 
    // (i.e., if any array is longer than another because the experiment stopped in some particular place, 
    // ensure the odd element out can be identified)
    // TODO: RDD/JPB: ensure that stim events are set off as close as possible to this callback being called
    //                might be simplest to let this function control the stim?
    //                do/can we receive a callback when a stim event ends?
    abs_event_times += current_time_ms;

    classif_state = task_classifier_settings.cl_type;
    // RC_DEBOUT(RC::RStr("ExperCPS::ClassifierDecision_Handler\n\n"));

    // TODO: RDD: separate Bayesian search objects for different stim locations
    // TODO: RDD: add code for computing final optimal parameters after experiment completed
    //      could be completed entirely off-line, probably simplest, but would be nice to simply compute live
    //      would then need to end experiment for subjet while continuing computation, more complicated...
    // TODO: RDD: need to set classifyms and id for all calls to classifier class
    // TODO define minimum numbers of events for each stim location/sham for experiment

    // store all results
    classif_results += result;
    stim_event_flags += stim_event;
    exper_classif_settings += task_classifier_settings;

    ClassificationType next_classif_state;
    // TODO logging
    if (classif_state == ClassificationType::STIM ||
        classif_state == ClassificationType::SHAM) {  // received pre-stim/pre-sham classification event
      // TODO ensure/confirm with James that <stim_event == classif_prob < 0.5> and not <stim_event == whether stim event actually occurred, which would filter out sham events in addition to high memory states>
      // this could be a tangible difference between the NOSTIM/SHAM events, that stim_event := false for NOSTIM, stim_event := classif_prob < 0.5 for SHAM
      if (stim_event) { // stim event occurred or would have occurred if the event were not a sham
        // TODO handle cases where say stim duration not equal between given and used params...
        //      use assert as a placeholder for initial testing
        // TODO RDD/RC: when will CereStim change received stim parameters internally and will we receive that
        //              info back in some way?
        // assert(stim_params == stim_profiles[cur_ev]);
        if (stim_params != stim_profiles[cur_ev]) {
          // TODO: RDD: add warning message here
          warning
          stim_profiles[cur_ev] = stim_params;
        }
        prev_sham = classif_state == ClassificationType::SHAM;
        // get post-stim classifier output
        // TODO: RDD/JPB: when will stim event offset be relative to this callback being called?
        //                can we get that info from CereStim? Or can we use nominal duration instead?
        uint64_t stim_offset_ms = abs_event_times[abs_event_times.size()] + stim_params.duration;
        next_min_event_time = stim_offset_ms + poststim_classif_lockout_ms;
        next_classif_state = ClassificationType::NOSTIM;
      }
      else { // good memory state detected and stim event would not have occurred
        // TODO: RDD/JPB: add some timing control? or maximize classification rate? in any case should record event times
        //                to analyze event time frequencies, may want to add some jitter if fairly low variance
        // keep classifying until a poor memory state is detected
        next_min_event_time = cur_time_ms;
        next_classif_state = classif_state;
        // TODO always access results[], classif_results[], and any other event arrays with last event
      }
    }
    else if (classif_state == ClassificationType::NOSTIM) { // received post-stim/post-sham classification event
      // TODO get stim_offset_ms from classifier or from CereStim handler class, don't compute here if possible
      // though errors wouldn't accumulate too much
      uint64_t stim_offset_ms = abs_event_times[abs_event_times.size() - 1] + stim_params.duration;
      next_min_event_time = stim_offset_ms + stim_lockout_ms;

      double biomarker = result - classif_results[classif_results.size() - 1];
      // TODO add struct for storing everything related to a full pre-post event (i.e., pre-stim classifier event time, stim event time, post-stim classifier time, classifier results, biomarker, stim parameters, ideally Bayesian search hps? definitely should store those separately...)
      //      otherwise could just parse after the fact, this would prevent misparsing...
      if (!prev_sham) {
        UpdateSearch(stim_profiles[cur], exp_events[cur], biomarker);
        GetNextEvent();
        cur_ev++;
        
        // run pre-event (stim configuration) as soon as stim parameters are available
        if (cur_ev < exp_events.size()) {
          hndl->stim_worker.ConfigureStimulation(stim_profiles[cur_ev]);
        }
        else {
          Throw_RC_Error("Event requested before being allocated by the search process.");
        }
      }
      
      // TODO update to select between sham and stim events based on some criteria
      // Randomize all the experiment events.
      // exp_events.Shuffle();
      if (cur_ev % 5) {
        next_classif_state = ClassificationType::STIM;
      }
      else {
        next_classif_state = ClassificationType::SHAM;
      }
    }
    else if (classif_state == ClassificationType::NORMALIZE) {
      next_min_event_time = cur_time_ms + normalize_lockout_ms;
      if (results.size() < n_normalize_events) {
        // TODO log event info
        // TODO add some jitter? add (explicit) jitter to timeouts in general?
        next_classif_state = ClassificationType::NORMALIZE;
      }
      else if (results.size() == n_normalize_events) {
        // TODO log event info
        // TODO update to select between sham and stim events based on some criteria
        next_classif_state = ClassificationType::STIM;
      }
      else {
        Throw_RC_Error("Normalization event requested after n_normalize_events normalization events completed.");
      }
    }
    else {
      Throw_RC_Error("Invalid classification type received.");
    }

    // run next classification result (which conditionally calls for stimulation events)
    if (next_min_event_time > experiment_duration) {
      EndExperiment();
    }
    WaitUntil(next_min_event_time);
    // TODO: RDD?JPB: use id = 0 for now, how much performance improvement would classifier queueing add?
    hndl->task_classifier_manager->ProcessClassifierEvent(
        next_classif_state, classify_ms, classif_id);
    classif_id++;

    // TODO are the next 15 or so commented lines all for logging? how are we logging in general?
    // JSONFile data;
    // data.Set(result, "result");
    // data.Set(stim, "decision");

    // const RC::RStr type = [&] {
    //     switch (task_classifier_settings.cl_type) {
    //       case ClassificationType::STIM: return "STIM_DECISON";
    //       case ClassificationType::SHAM: return "SHAM_DECISON";
    //       default: Throw_RC_Error("Invalid classification type received.");
    //     }
    // }();

    // auto resp = MakeResp(type, task_classifier_settings.classif_id, data);
    // hndl->event_log.Log(resp.Line());

    // code for stimulating:
    // if (stim_type && stim) {
    //   // TODO: JPB: (need) Temporarily remove call to stimulate
    //   hndl->stim_worker.Stimulate();
    // }
  }

}
