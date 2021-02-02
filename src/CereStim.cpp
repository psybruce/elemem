// 2019, Ryan A. Colyer
// Computational Memory Lab, University of Pennsylvania
//
// This file provides C++ RAII wrappers for the CereStim API by Blackrock
// Microsystems
//
/////////////////////////////////////////////////////////////////////////////

#include <algorithm>
#include <iostream>
#include <iterator>
#include <vector>

#include "CereStim.h"


namespace CML {
  CereStim::CereStim() {
  }

  void CereStim::Open() {
    if (is_open) {
      Close();
    }

    uint64_t num_devices = 1024;
    std::vector<uint32_t> device_serial_nums(num_devices);
    ErrorCheck(
      CS_ScanForDevices(&num_devices, device_serial_nums.data())
    );
    device_serial_nums.resize(num_devices);

    if (device_serial_nums.size() < 1) {
      throw CSException(-9);
    }

    // Select first device.
    // If future requirements appear for multiple stimulators,
    // the serial numbers should be parsed here.
    uint32_t dev = 0;
    ErrorCheck(
      CS_SetDevice(dev)
    );

    ErrorCheck(
      CS_Connect()
    );

    is_open = true;

    stim_width_us = 300;
    CSMaxValues max_vals;
    max_vals.voltage = 15; // (0.5 + 0.6*15)V = 9.5V
    max_vals.amplitude = 3500; // 35*100uA.
    //max_vals.phase_charge = 100*max_vals.amplitude * stim_width_us + 1;
    max_vals.phase_charge = 20000000; // Won't go above 1.05e6 with 300us
    max_vals.frequency = 1000; // Hz
    was_active = false;

    SetMaxValues(max_vals);    
  }

  CereStim::~CereStim() {
    Close();
  }

  void CereStim::Close() {
    if (is_open) {
      if (was_active) {
        CS_Stop();
      }
      CS_Disconnect();

      is_open = false;
    }
  }

  void CereStim::BeOpen() {
    if (!is_open) {
      Open();
    }
  }

  CSMaxValues CereStim::GetMaxValues() {
    BeOpen();

    CSMaxValues ret;

    ErrorCheck(
      CS_GetMaxValues(&ret.voltage, &ret.amplitude, &ret.phase_charge,
        &ret.frequency)
    );

    return ret;
  }

  void CereStim::SetMaxValues(CSMaxValues max_vals) {
    BeOpen();

    ErrorCheck(
      CS_SetMaxValues(max_vals.voltage, max_vals.amplitude,
        max_vals.phase_charge, max_vals.frequency)
    );
  }

  void CereStim::ConfigureStimulation(CSStimProfile profile) {
    BeOpen();

    StopStimulation();

    struct FreqDurAmp {
      uint32_t frequency;
      uint32_t duration;
      uint16_t amplitude;
      bool operator==(const FreqDurAmp& other) const {
          return frequency == other.frequency &&
                 duration == other.duration &&
                 amplitude == other.amplitude;
      }
    };
    std::vector<FreqDurAmp> fda_vec;

    size_t prof_size = profile.stim_profile.size();

    size_t max_bipolar_pairs = (128-2)/2;  // From stim script max length.
    if (prof_size > (128-2)/2) {
      throw std::runtime_error("Only " + std::to_string(max_bipolar_pairs) +
          " bipolar pairs can be stimulated.");
    }

    // Verify electrodes are all unique.
    std::vector<uint8_t> uniqueness_check(256);
    for (auto& prof: profile.stim_profile) {
      if (uniqueness_check.at(prof.electrode_pos) ||
          uniqueness_check.at(prof.electrode_neg) ||
          prof.electrode_pos == prof.electrode_neg) {
        throw std::runtime_error("Stimulation channels must be unique");
      }
      uniqueness_check.at(prof.electrode_pos) = 1;
      uniqueness_check.at(prof.electrode_neg) = 1;
    }


    std::vector<size_t> pattern_index(prof_size);
    
    for (size_t i=0; i<prof_size; i++) {
      // CS can only store 15 stimulus patterns.
      // For bipolar stimulation, only 7 pairs can be stored.
      // Extract and index the unique ones.
      auto& chan = profile.stim_profile[i];
      FreqDurAmp fda{chan.frequency, chan.duration, chan.amplitude};
      auto res = std::find(fda_vec.begin(), fda_vec.end(), fda);
      if (res == fda_vec.end()) {
        // 15 / 2 = 7 for bipolar.
        if (fda_vec.size() >= 7) {
          throw std::runtime_error("Only 7 variations of stimulation "
              "amplitude, frequency, and duration allowed.");
        }
        fda_vec.push_back(fda);
        res = std::prev(fda_vec.end());
      }
      auto pat_index = res - fda_vec.begin();
      pattern_index[i] = size_t(pat_index);
    }

    for (size_t i=0; i<fda_vec.size(); i++) {
      auto& afd = fda_vec[i];
      uint16_t interphase = 53;
      uint64_t pulses_64 = (uint64_t(afd.duration) * afd.frequency) / 1000000;
      if (pulses_64 < 1) {
        pulses_64 = 1;
      }
      if (pulses_64 > 255) {
        throw std::runtime_error(std::to_string(afd.duration) + "us pulse "
            "duration too long for " + std::to_string(afd.frequency) + "Hz "
            "stimulus.");
      }
      uint8_t pulses = uint8_t(pulses_64);
      // Anodic/positive first waveform
      ErrorCheck(
        CS_ConfigureStimulusPattern(uint16_t(2*i+1), 0, pulses, afd.amplitude,
          afd.amplitude, stim_width_us, stim_width_us, afd.frequency,
          interphase)
      );
      // Cathodic/negative first waveform
      ErrorCheck(
        CS_ConfigureStimulusPattern(uint16_t(2*i+2), 1, pulses, afd.amplitude,
          afd.amplitude, stim_width_us, stim_width_us, afd.frequency,
          interphase)
      );
    }

    ErrorCheck(
      CS_BeginningOfSequence()
    );
    ErrorCheck(
      CS_BeginningOfGroup()
    );


    for (size_t i=0; i<prof_size; i++) {
      // anode-first / then cathode-first, same settings.
      ErrorCheck(
        CS_AutoStimulus(profile.stim_profile[i].electrode_pos,
          uint16_t(2*(pattern_index[i])+1))
      );
      ErrorCheck(
        CS_AutoStimulus(profile.stim_profile[i].electrode_neg,
          uint16_t(2*(pattern_index[i])+2))
      );
    }

    ErrorCheck(
      CS_EndOfGroup()
    );
    ErrorCheck(
      CS_EndOfSequence()
    );
  }

  void CereStim::Stimulate() {
    BeOpen();

    StopStimulation();

    was_active = true;
    ErrorCheck(
      CS_Play(1)
    );
  }

  void CereStim::StopStimulation() {
    BeOpen();

    if (was_active) {
      ErrorCheck(
        CS_Stop()
      );
    }
    was_active = false;
  }

  void CereStim::ErrorCheck(int err) {
    if (err == 0) {
      return;
    }

    throw CSException(err);
  }


  CSException::~CSException() {
  }

  std::string CSException::CodeToString(int err) {
    switch(err) {
      case 1:
        return "Early returned warning";
      case 0:
        return "Successful operation";
      case -1:
        return "Not implemented";
      case -2:
        return "Unknown error";
      case -3:
        return "Invalid handle";
      case -4:
        return "Null pointer";
      case -5:
        return "CereStim unit not found";
        //return "Invalid intrface specified or interface not supported";
      case -6:
        return "Timeout in creating the interface";
      case -7:
        return "Device with that address already connected.";
      case -8:
        return "Invalid parameters";
      case -9:
        return "Stim is disconnected, invalid operation";
      case -10:
        return "Stim is connected, invalid operation";
      case -11:
        return "Stim is attached, invalid operation";
      case -12:
        return "Stim is detached, invalid operation";
      case -13:
        return "Cannot register for device change notification";
      case -14:
        return "Invalid command";
      case -15:
        return "Cannot open interface for write";
      case -16:
        return "Cannot open interface for read";
      case -17:
        return "Cannot write command to the interface";
      case -18:
        return "Cannot read command from the interface";
      case -19:
        return "Invalid module number specified";
      case -20:
        return "Invalid callback type";
      case -21:
        return "Callback register/unregister failed";
      case -22:
        return "CereStim Firmware version not supported by SDK Library Version";
      case -23:
        return "Frequency or Period is zero and unable to be converted";
      case -24:
        return "No physical device has been set. See setDevice() for help.";
      case -100:
        return "Comamnd result not OK";
      case -102:
        return "Sequence Error";
      case -103:
        return "Invalid Trigger";
      case -104:
        return "Invalid Channel";
      case -105:
        return "Invalid Configuration";
      case -106:
        return "Invalid Number";
      case -107:
        return "Invalid Read/Write";
      case -108:
        return "Invalid Voltage";
      case -109:
        return "Invalid Amplitude";
      case -110:
        return "Invalid AF/CF";
      case -111:
        return "Invalid Pulses";
      case -112:
        return "Invalid Width";
      case -113:
        return "Invalid Interpulse";
      case -114:
        return "Invalid Interphase";
      case -115:
        return "Invalid Fast Discharge";
      case -116:
        return "Invalid Module";
      case -117:
        return "More Stimuli than Modules";
      case -118:
        return "Module not Available";
      case -119:
        return "Channel already used in Group";
      case -120:
        return "Configuration not Active";
      case -121:
        return "Empty Config";
      case -122:
        return "Phases not Balanced";
      case -123:
        return "Phase Charge Greater than Max";
      case -124:
        return "Amplitude Greater than Max";
      case -125:
        return "Width Greater than Max";
      case -126:
        return "Voltage Greater than Max";
      case -127:
        return "Module already disabled can't disable it";
      case -128:
        return "Module already enabled can't reenable it";
      case -129:
        return "Invalid Frequency";
      case -130:
        return "The frequency is greater than the max value allowed";
      case -131:
        return "Device locked due to hardware mismatch or not being configured";
      default:
        return "Unrecognized error code";
    }
  }


  CSStimProfile& CSStimProfile::operator+=(const CSStimChannel& chan) {
    stim_profile.push_back(chan);
    return *this;
  }
}

