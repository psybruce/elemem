#include "FeatureFilters.h"
#include "Popup.h"
#include "Utils.h"
#include <cmath>


namespace CML {
  BinnedData::BinnedData(size_t binned_sampling_rate, size_t binned_sample_len, size_t leftover_sampling_rate, size_t leftover_sample_len) {
    out_data = RC::MakeAPtr<EEGDataRaw>(binned_sampling_rate, binned_sample_len);
    leftover_data = RC::MakeAPtr<EEGDataRaw>(leftover_sampling_rate, leftover_sample_len);
  }


  // TODO: JPB: (refactor) Make this take const refs
  FeatureFilters::FeatureFilters(RC::Data1D<BipolarPair> bipolar_reference_channels,
      ButterworthSettings butterworth_settings, MorletSettings morlet_settings,
      NormalizePowersSettings np_set) 
	  : bipolar_reference_channels(bipolar_reference_channels),
	  normalize_powers(np_set) {
    butterworth_transformer.Setup(butterworth_settings);
    morlet_transformer.Setup(morlet_settings);
  }

  /// Bins EEGDataRaw from one sampling rate to another
  /** @param in_data the EEGDataRaw to be binned
    * @param new_sampling_rate the new sampling rate
    * @return EEGDataRaw that has been binned to the new sampling rate
  */
  RC::APtr<BinnedData> FeatureFilters::BinData(RC::APtr<const EEGDataRaw> in_data, size_t new_sampling_rate) {
    if (new_sampling_rate == 0)
      Throw_RC_Type(Bounds, "New binned sampling rate cannot be 0");

    if (new_sampling_rate > in_data->sampling_rate) {
      Throw_RC_Error(("The new sampling rate (" + RC::RStr(new_sampling_rate) + ") " +
          "is greater than the sampling rate of in_data (" + RC::RStr(in_data->sampling_rate)).c_str());
    }

    // TODO: JPB: (feature) Add ability to handle sampling ratios that aren't true multiples
    size_t sampling_ratio = in_data->sampling_rate / new_sampling_rate;
    size_t out_sample_len = in_data->sample_len / sampling_ratio;
    size_t leftover_sample_len = in_data->sample_len % sampling_ratio;
    auto binned_data = RC::MakeAPtr<BinnedData>(new_sampling_rate, out_sample_len, in_data->sampling_rate, leftover_sample_len);

    auto& in_datar = in_data->data;
    auto& out_datar = binned_data->out_data->data;
    auto& leftover_datar = binned_data->leftover_data->data;
    out_datar.Resize(in_datar.size());
    leftover_datar.Resize(in_datar.size());

    // Separate accum lambda created for move sematics optimization
    auto accum_plus = [](u32 sum, size_t val) { return std::move(sum) + val; };
    RC_ForIndex(i, out_datar) { // Iterate over channels
      auto& in_events = in_datar[i];
      auto& out_events = out_datar[i];
      auto& leftover_events = leftover_datar[i];

      if (in_events.IsEmpty()) { continue; }
      binned_data->out_data->EnableChan(i);
      binned_data->leftover_data->EnableChan(i);

      // Bin the events
      RC_ForIndex(j, out_events) { // Iterate over events
        size_t start = j * sampling_ratio;
        size_t end = (j+1) * sampling_ratio - 1;
        size_t items = sampling_ratio;
        out_events[j] = std::accumulate(&in_events[start], &in_events[end]+1,
                          0, accum_plus) / items;
      }

      // Get the leftover events
      if (leftover_sample_len != 0) {
        size_t start = in_events.size() - leftover_sample_len;
        size_t end = in_events.size() - 1;
        size_t items = end - start + 1;
        leftover_events.CopyFrom(in_events, start, items);
      }
    }

    return binned_data;
  }

  /// Bins EEGDataRaw from one sampling rate to another
  /** @param in_data the EEGDataRaw to be binned
    * @param new_sampling_rate the new sampling rate
    * @return EEGDataRaw that has been binned to the new sampling rate
  */
  RC::APtr<BinnedData> FeatureFilters::BinData(RC::APtr<const EEGDataRaw> rollover_data, RC::APtr<const EEGDataRaw> in_data, size_t new_sampling_rate) {
    if (new_sampling_rate == 0)
      Throw_RC_Type(Bounds, "New binned sampling rate cannot be 0");

    if (new_sampling_rate > in_data->sampling_rate) {
      Throw_RC_Error(("The new sampling rate (" + RC::RStr(new_sampling_rate) + ") " +
          "is greater than the sampling rate of in_data (" + RC::RStr(in_data->sampling_rate)).c_str());
    }

    if (rollover_data->sampling_rate != in_data->sampling_rate) {
      Throw_RC_Error(("The sampling rate of rollover_data (" + RC::RStr(rollover_data->sampling_rate) + ") " +
          "and the sampling rate of in_data (" + RC::RStr(in_data->sampling_rate) + ") are not the same").c_str());
    }

    size_t total_in_sample_len = rollover_data->sample_len + in_data->sample_len;
    EEGDataRaw total_in_data(in_data->sampling_rate, total_in_sample_len);

    // Make total in data that is a appending of in_data to rollover_data
    // TODO: JPB: (feature)(optimization) There is a more efficient way to do this that doesn't involve all these copy operations
    auto& rollover_datar = rollover_data->data;
    auto& in_datar = in_data->data;
    auto& total_in_datar = total_in_data.data;
    total_in_datar.Resize(in_datar.size());
    RC_ForIndex(i, in_datar) { // Iterate over channels
      if (!in_datar.IsEmpty()) {
        total_in_data.EnableChan(i);
        total_in_datar[i].CopyAt(0, rollover_datar[i]);
        total_in_datar[i].CopyAt(rollover_data->sample_len, in_datar[i]);
      }
    }

    // TODO: JPB: (feature) Add ability to handle sampling ratios that aren't true multiples
    size_t sampling_ratio = total_in_data.sampling_rate / new_sampling_rate;
    size_t out_sample_len = total_in_data.sample_len / sampling_ratio;
    size_t leftover_sample_len = total_in_data.sample_len % sampling_ratio;
    auto binned_data = RC::MakeAPtr<BinnedData>(new_sampling_rate, out_sample_len, total_in_data.sampling_rate, leftover_sample_len);

    auto& out_datar = binned_data->out_data->data;
    auto& leftover_datar = binned_data->leftover_data->data;
    out_datar.Resize(total_in_datar.size());
    leftover_datar.Resize(total_in_datar.size());

    // Separate accum lambda created for move sematics optimization
    auto accum_plus = [](u32 sum, size_t val) { return std::move(sum) + val; };
    RC_ForIndex(i, out_datar) { // Iterate over channels
      auto& total_in_events = total_in_datar[i];
      auto& out_events = out_datar[i];
      auto& leftover_events = leftover_datar[i];

      if (total_in_events.IsEmpty()) { continue; }
      binned_data->out_data->EnableChan(i);
      binned_data->leftover_data->EnableChan(i);

      // Bin the events
      RC_ForIndex(j, out_events) { // Iterate over events
        size_t start = j * sampling_ratio;
        size_t end = (j+1) * sampling_ratio - 1;
        size_t items = sampling_ratio;
        out_events[j] = std::accumulate(&total_in_events[start], &total_in_events[end]+1,
                          0, accum_plus) / items;
      }

      // Get the leftover events
      if (leftover_sample_len != 0) {
        size_t start = total_in_events.size() - leftover_sample_len;
        size_t end = total_in_events.size() - 1;
        size_t items = end - start + 1;
        leftover_events.CopyFrom(total_in_events, start, items);
      }
    }

    return binned_data;
  }

  /// Bins EEGDataRaw from one sampling rate to another
  /** @param in_data the EEGDataRaw to be binned
    * @param new_sampling_rate the new sampling rate
    * @return EEGDataRaw that has been binned to the new sampling rate
  */
  RC::APtr<EEGDataRaw> FeatureFilters::BinDataAvgRollover(RC::APtr<const EEGDataRaw> in_data, size_t new_sampling_rate) {
    if (new_sampling_rate == 0)
      Throw_RC_Type(Bounds, "New binned sampling rate cannot be 0");

    if (new_sampling_rate > in_data->sampling_rate) {
      Throw_RC_Error(("The new sampling rate (" + RC::RStr(new_sampling_rate) + ") " +
          "is greater than the sampling rate of in_data (" + RC::RStr(in_data->sampling_rate)).c_str());
    }

    // TODO: JPB: (feature) Add ability to handle sampling ratios that aren't true multiples
    size_t sampling_ratio = in_data->sampling_rate / new_sampling_rate;
    size_t new_sample_len = CeilDiv(in_data->sample_len, sampling_ratio);

    auto out_data = RC::MakeAPtr<EEGDataRaw>(new_sampling_rate, new_sample_len);
    auto& in_datar = in_data->data;
    auto& out_datar = out_data->data;
    out_datar.Resize(in_datar.size());

    // Separate accum lambda created for move sematics optimization
    auto accum_plus = [](u32 sum, size_t val) { return std::move(sum) + val; };
    RC_ForIndex(i, out_datar) { // Iterate over channels
      auto& in_events = in_datar[i];
      auto& out_events = out_datar[i];

      if (in_events.IsEmpty()) { continue; }
      out_data->EnableChan(i);

      RC_ForIndex(j, out_events) { // Iterate over events
        if (j < out_events.size() - 1) {
          size_t start = j * sampling_ratio;
          size_t end = (j+1) * sampling_ratio - 1;
          size_t items = sampling_ratio;
          out_events[j] = std::accumulate(&in_events[start], &in_events[end]+1,
                            0, accum_plus) / items;
        } else { // Last block could have leftover samples
          size_t start = j * sampling_ratio;
          size_t end = in_events.size() - 1;
          size_t items = end - start + 1;
          out_events[j] = std::accumulate(&in_events[start], &in_events[end]+1,
                            0, accum_plus) / items;
        }
      }
    }

    return out_data;
  }

  /// Converts an EEGDataRaw of electrode channels into EEGDataDouble of electrode channels
  /** @param EEGDataRaw of electrode channels
    * @return EEGDataDouble of electrode channels
    */
  RC::APtr<EEGDataDouble> FeatureFilters::BipolarPassthrough(RC::APtr<const EEGDataRaw>& in_data) {
    auto out_data = RC::MakeAPtr<EEGDataDouble>(in_data->sampling_rate, in_data->sample_len);
    auto& in_datar = in_data->data;
    auto& out_datar = out_data->data;
    out_datar.Resize(in_datar.size());

    RC_ForIndex(i, out_datar) { // Iterate over channels
      if (in_datar[i].IsEmpty()) { continue; }
      out_data->EnableChan(i);
      out_datar[i].CopyFrom(in_datar[i]);
    }

    return out_data;
  }

  /// Converts an EEGDataRaw of electrode channels into EEGDataDouble of bipolar pair channels
  /** @param EEGDataRaw of electrode channels
    * @param List of bipolar pairs
    * @return EEGDataDouble of bipolar pair channels
    */
  RC::APtr<EEGDataDouble> FeatureFilters::BipolarReference(RC::APtr<const EEGDataRaw>& in_data, RC::Data1D<BipolarPair> bipolar_reference_channels) {
    auto out_data = RC::MakeAPtr<EEGDataDouble>(in_data->sampling_rate, in_data->sample_len);
    auto& in_datar = in_data->data;
    auto& out_datar = out_data->data;
    size_t chanlen = bipolar_reference_channels.size();
    out_datar.Resize(chanlen);

    RC_ForIndex(i, out_datar) { // Iterate over channels
      uint8_t pos = bipolar_reference_channels[i].pos;
      uint8_t neg = bipolar_reference_channels[i].neg;

      if (pos >= in_datar.size()) { // Pos channel not in data
        Throw_RC_Error(("Positive channel " + RC::RStr(pos) +
              " is not a valid channel. The number of channels available is " +
              RC::RStr(in_datar.size())).c_str());
      } else if (neg >= in_datar.size()) { // Neg channel not in data
        Throw_RC_Error(("Negative channel " + RC::RStr(neg) +
              " is not a valid channel. The number of channels available is " +
              RC::RStr(in_datar.size())).c_str());
      } else if (in_datar[pos].IsEmpty()) { // Pos channel is empty
        Throw_RC_Error(("Positive channel " + RC::RStr(pos) +
              " does not have any data.").c_str());
      } else if (in_datar[neg].IsEmpty()) { // Neg channel is empty
        Throw_RC_Error(("Negative channel " + RC::RStr(neg) +
              " does not have any data.").c_str());
      } else if (in_datar[pos].size() != in_datar[neg].size()) { // Pos and Neg channel sizes don't match
        Throw_RC_Error(("Size of positive channel " + RC::RStr(pos) +
              " (" + RC::RStr(in_datar[pos].size()) + ") " +
              "and size of negitive channel " + RC::RStr(neg) +
              " (" + RC::RStr(in_datar[neg].size()) + ") " +
              "are different").c_str());
      }

      // Don't skip empty channels, they are errors above
      out_data->EnableChan(i);

      auto& out_events = out_datar[i];
      RC_ForIndex(j, out_events) {
        out_events[j] = static_cast<double>(in_datar[pos][j]) - static_cast<double>(in_datar[neg][j]);
      }
    }

    return out_data;
  }

  // Note: Watch out for overflow on smaller types
  template<typename T>
  RC::Data1D<T> FeatureFilters::Differentiate(const RC::Data1D<T>& in_data, size_t order) {
    if (order == 0) { Throw_RC_Error("The order cannot be 0."); }

    if (order >= in_data.size()) {
      Throw_RC_Error(("The order (" + RC::RStr(order) + ") " +
            "is greater than or equal to the number of samples in the data "
            "(" + RC::RStr(in_data.size()) + ")").c_str());
    }

    // Take derivative
    auto out_data = RC::Data1D<T>();
    out_data.Resize(in_data.size() - 1);
    RC_ForRange(i, 0, out_data.size()) { // Iterate over events
      out_data[i] = in_data[i+1] - in_data[i];
    }

    // Base case
    if (order == 1) { return out_data; }
    // Recurse for next order
    return Differentiate(out_data, order-1);
  }

  /// Find the channels with artifacting using a ordered derivate test
  /** @param in_data The data to be evaluated for artifacting
    * @param threshold The number of events where the order derivative is equal to 0
    * @param order The number of events to use in the derivative
    * @return A boolean mask over the channels which specifies which channels show artifacts
    */
  RC::APtr<RC::Data1D<bool>> FeatureFilters::FindArtifactChannels(RC::APtr<const EEGDataDouble>& in_data, size_t threshold, size_t order) {
    if (order >= in_data->sample_len) {
      Throw_RC_Error(("The order (" + RC::RStr(order) + ") " +
            "is greater than or equal to the number of samples in the data "
            "(" + RC::RStr(in_data->sample_len) + ")").c_str());
    }

    if (threshold >= (in_data->sample_len - order)) {
      Throw_RC_Error(("The threshold (" + RC::RStr(threshold) + ") " +
            "is greater than or equal to the number of samples in the data minus the order"
            "(" + RC::RStr(in_data->sample_len - order) + "), " +
            "making it impossible for the threshold to occur.").c_str());
    }

    auto out_data = RC::MakeAPtr<RC::Data1D<bool>>();
    auto& in_datar = in_data->data;
    auto& out_datar = *out_data;
    size_t chanlen = in_datar.size();
    out_data->Resize(chanlen);

    auto accum_eq_zero_plus = [](size_t sum, double val) { return std::move(sum) + static_cast<size_t>(val == 0); };
    //auto accum_eq_zero_plus = [](size_t sum, double val) { RC_DEBOUT(val, static_cast<size_t>(val == 0), sum); return std::move(sum) + static_cast<size_t>(val == 0); };
    RC_ForIndex(i, in_datar) { // Iterate over channels
      auto& in_events = in_datar[i];
      auto& out_event = out_datar[i];

      if (in_events.IsEmpty()) { // Set empty channels to True
        out_event = true;
        continue;
      }

      // Take the ordered derivative
      auto deriv_data = Differentiate<double>(in_events, order);
      //RC_DEBOUT(RC::RStr::Join(deriv_data, ", ") + "\n");

      // Sum and threshold the data
      // TODO: JPB: (refactor) Figure out why the line below this comment doesn't work
      //size_t eq_zero = std::accumulate(deriv_data.begin(), deriv_data.end(), 0, accum_eq_zero_plus);
      size_t eq_zero = std::accumulate(&deriv_data[0], &deriv_data[deriv_data.size()-1]+1, 0, accum_eq_zero_plus);
      //RC_DEBOUT(eq_zero);
      out_event = eq_zero > threshold;
    }

    return out_data;
  }

  /// Zero the channels with artifacts using a privided artifact mask
  /** @param in_data The data to be evaluated for artifacting
    * @param artifact_channel_mask The indicator for each channel if it had artifacts or not
    * @return The provided data with the artifact channels zeroed
    */
  RC::APtr<EEGPowers> FeatureFilters::ZeroArtifactChannels(RC::APtr<const EEGPowers>& in_data, RC::APtr<const RC::Data1D<bool>>& artifact_channel_mask) {

    auto& in_datar = in_data->data;
    size_t freqlen = in_datar.size3();
    size_t chanlen = in_datar.size2();
    size_t eventlen = in_datar.size1();

    if (chanlen != artifact_channel_mask->size()) {
      Throw_RC_Error(("The channel length of the in_data (" + RC::RStr(chanlen) + ") " +
            "is not equal to the number of channels in the artifact_channel_mask "
            "(" + RC::RStr(artifact_channel_mask->size()) + ")").c_str());
    }

    auto out_data = RC::MakeAPtr<EEGPowers>(in_data->sampling_rate, eventlen, chanlen, freqlen);
    auto& out_datar = out_data->data;

    RC_ForRange(i, 0, freqlen) { // Iterate over frequencies
      RC_ForRange(j, 0, chanlen) { // Iterate over channels
        if ((*artifact_channel_mask)[j]) {
          out_datar[i][j].Zero();
        } else {
          out_datar[i][j].CopyFrom(in_datar[i][j]);
        }
      }
    }

    return out_data;
  }

  /// Mirrors both ends of the EEGDataDouble for the provided number of seconds
  /** Note that when mirroring, you do not include the items bein mirror over
    * Ex: 0, 1, 2, 3 with a mirroring of 2 samples becomes 2, 1, 0, 1, 2, 3, 2, 1
    * @param The data to be mirrored
    * @param Duration to mirror each side for
    * @return The mirrored EEGDataDouble
    */
  RC::APtr<EEGDataDouble> FeatureFilters::MirrorEnds(RC::APtr<const EEGDataDouble>& in_data, size_t mirrored_duration_ms) {
    size_t num_mirrored_samples = mirrored_duration_ms * in_data->sampling_rate / 1000;
    size_t in_sample_len = in_data->sample_len;
    size_t out_sample_len = in_sample_len + num_mirrored_samples * 2;

    if (num_mirrored_samples >= in_sample_len) {
      Throw_RC_Error(("The number of samples to be mirrored "
            "(" + RC::RStr(num_mirrored_samples) + ") " +
            "is greater than or equal to the number of samples in the data "
            "(" + RC::RStr(in_sample_len) + ")").c_str());
    }

    auto out_data = RC::MakeAPtr<EEGDataDouble>(in_data->sampling_rate, out_sample_len);
    auto& in_datar = in_data->data;
    auto& out_datar = out_data->data;
    size_t chanlen = in_datar.size();
    out_datar.Resize(chanlen);

    RC_ForIndex(i, in_datar) { // Iterate over channels
      auto& in_events = in_datar[i];
      auto& out_events = out_datar[i];

      if (in_events.IsEmpty()) { continue; } // Skip empty channels
      out_data->EnableChan(i);

      if (num_mirrored_samples >= in_sample_len) {
        Throw_RC_Error(("Number of events to mirror "
              "(" + RC::RStr(num_mirrored_samples) + ") " +
              "is greater than number of samples in channel " + RC::RStr(i) + " " +
              "(" + RC::RStr(in_sample_len) + ")").c_str());
      }

      // Copy starting samples in reverse, skipping the first item
      RC_ForRange(i, 0, num_mirrored_samples) {
        out_events[i] = in_events[num_mirrored_samples-i];
      }

      // Copy all original samples verbatim for the middle
      out_events.CopyAt(num_mirrored_samples, in_events);

      // Copy ending samples in reverse, skipping the last item
      size_t start_pos = num_mirrored_samples + in_sample_len;
      RC_ForRange(i, 0, num_mirrored_samples) {
        out_events[start_pos+i] = in_events[in_sample_len-i-2];
      }
    }

    return out_data;
  }

  /// Remove mirrored data from both ends of the EEGPowers for the provided number of seconds
  /** @param The data to be un-mirrored
    * @param Duration to mirror each side for
    * @return The un-mirrored EEGDataDouble
    */
  RC::APtr<EEGPowers> FeatureFilters::RemoveMirrorEnds(RC::APtr<const EEGPowers>& in_data, size_t mirrored_duration_ms) {
    auto& in_datar = in_data->data;
    size_t num_mirrored_samples = mirrored_duration_ms * in_data->sampling_rate / 1000;
    size_t freqlen = in_datar.size3();
    size_t chanlen = in_datar.size2();
    size_t in_eventlen = in_datar.size1();
    size_t out_eventlen = in_eventlen - num_mirrored_samples * 2;

    if (num_mirrored_samples >= out_eventlen) {
      Throw_RC_Error(("The number of samples to be mirrored "
            "(" + RC::RStr(num_mirrored_samples) + ") " +
            "is greater than or equal to the number of samples in the non-mirrored data "
            "(" + RC::RStr(out_eventlen) + ")").c_str());
    }

    auto out_data = RC::MakeAPtr<EEGPowers>(in_data->sampling_rate, out_eventlen, chanlen, freqlen);
    auto& out_datar = out_data->data;

    RC_ForRange(i, 0, freqlen) { // Iterate over frequencies
      RC_ForRange(j, 0, chanlen) { // Iterate over channels
        out_datar[i][j].CopyFrom(in_datar[i][j], num_mirrored_samples, out_eventlen);
      }
    }

    return out_data;
  }

  /// A log (base 10) transform of all powers in EEGPowers
  /** Note: A power could be zero due to constant signal across two electrodes that are part of bipolar pair
    * @param The data to be log transformed
    * @param Minimum power clamp (just before taking log) to avoid log singularity in case we get zero power
    * @return The log transformed data
    */
  RC::APtr<EEGPowers> FeatureFilters::Log10Transform(RC::APtr<const EEGPowers>& in_data, double min_power_clamp) {
    return Log10Transform(in_data, min_power_clamp, false);
  }

  /// A log (base 10) transform of all powers in EEGPowers
  /** Note: A power could be zero due to constant signal across two electrodes that are part of bipolar pair
    * @param The data to be log transformed
    * @param Minimum power clamp (just before taking log) to avoid log singularity in case we get zero power
    * @param This flag cause the function to use the minimum power clamp as an epilon that is added to each value before taking the log (SYS3 reproducibility)
    * @return The log transformed data
    */
  RC::APtr<EEGPowers> FeatureFilters::Log10Transform(RC::APtr<const EEGPowers>& in_data, double min_power_clamp, bool min_clamp_as_epsilon) {
    auto& in_datar = in_data->data;
    size_t freqlen = in_datar.size3();
    size_t chanlen = in_datar.size2();
    size_t eventlen = in_datar.size1();

    auto out_data = RC::MakeAPtr<EEGPowers>(in_data->sampling_rate, eventlen, chanlen, freqlen);
    auto& out_datar = out_data->data;

    RC_ForRange(i, 0, freqlen) { // Iterate over frequencies
      RC_ForRange(j, 0, chanlen) { // Iterate over channels
        RC_ForRange(k, 0, eventlen) { // Iterate over events
          double power = min_clamp_as_epsilon
            ? (in_datar[i][j][k] + min_power_clamp)
            : std::max(min_power_clamp, in_datar[i][j][k]);
          out_datar[i][j][k] = log10(power);
        }
      }   
    } 

    return out_data;
  }

  /// Average the EEGPowers over the time dimension (most inner dimension)
  /** Note: The time dimension will have a length of 1 in the returned data
    * @param The data to be averaged
    * @return The time averaged EEGPowers
    */
  RC::APtr<EEGPowers> FeatureFilters::AvgOverTime(RC::APtr<const EEGPowers>& in_data, bool ignore_inf_and_nan) {
    auto& in_datar = in_data->data;
    size_t freqlen = in_datar.size3();
    size_t chanlen = in_datar.size2();
    size_t in_eventlen = in_datar.size1();
    size_t out_eventlen = 1;

    auto out_data = RC::MakeAPtr<EEGPowers>(in_data->sampling_rate, out_eventlen, chanlen, freqlen);
    auto& out_datar = out_data->data;

    // Separate accum lambda created for move sematics optimization
    auto accum_plus = [](double sum, double val) { return std::move(sum) + val; };
    RC_ForRange(i, 0, freqlen) { // Iterate over frequencies
      RC_ForRange(j, 0, chanlen) { // Iterate over channels
        auto& in_events = in_datar[i][j];
        auto& out_events = out_datar[i][j];
        out_events[0] = std::accumulate(in_events.begin(), in_events.end(),
                          0.0, accum_plus) / static_cast<double>(in_eventlen);
        if (ignore_inf_and_nan && !std::isfinite(out_events[0])) {
          out_events[0] = 0;
          RC::RStr inf_nan_error = RC::RStr("The value at frequency ") + i + " and channel " + j + " is not finite";
          DEBLOG_OUT(inf_nan_error);
        }
      }
    }

    return out_data;
  }

  /// Average the EEGPowers over the time dimension (most inner dimension)
  /** @param data The EEGDataDouble to be run through all the filters
    * @param task_classifier_settings The settings for this classification chain
    */
  void FeatureFilters::Process_Handler(RC::APtr<const EEGDataRaw>& data, const TaskClassifierSettings& task_classifier_settings) {
    RC_DEBOUT(RC::RStr("FeatureFilters_Handler\n\n"));
    if (!callback.IsSet()) Throw_RC_Error("FeatureFilters callback not set");

    // This calculates the mirroring duration based on the minimum statistical morlet duration 
    size_t mirroring_duration_ms = morlet_transformer.CalcAvgMirroringDurationMs();

    auto bipolar_ref_data = BipolarReference(data, bipolar_reference_channels).ExtractConst();
    //auto bipolar_ref_data = BipolarPassthrough(data).ExtractConst();
    auto mirrored_data = MirrorEnds(bipolar_ref_data, mirroring_duration_ms).ExtractConst();
    auto morlet_data = morlet_transformer.Filter(mirrored_data).ExtractConst();
    auto unmirrored_data = RemoveMirrorEnds(morlet_data, mirroring_duration_ms).ExtractConst();
    auto log_data = Log10Transform(unmirrored_data, log_min_power_clamp).ExtractConst();
    auto avg_data = AvgOverTime(log_data, true).ExtractConst();

    // TODO: JPB (need) Remove debug code in FeatureFilters::Process_Handler
    data->Print(2);
    bipolar_ref_data->Print(2);
    mirrored_data->Print(2);
    morlet_data->Print(1, 2);
    unmirrored_data->Print(1, 2);
    log_data->Print(1, 2);
    avg_data->Print(1, 10);

    // Normalize Powers
    switch (task_classifier_settings.cl_type) {
      case ClassificationType::NORMALIZE:
        normalize_powers.Update(avg_data);
        normalize_powers.PrintStats(1, 10);
        break;
      case ClassificationType::STIM:
      case ClassificationType::SHAM:
      {
        auto norm_data = normalize_powers.ZScore(avg_data, true).ExtractConst();
        norm_data->Print(1, 10);

        // Perform 10th derivative test to find and remove artifact channels
        auto artifact_channel_mask = FindArtifactChannels(bipolar_ref_data, 10, 10).ExtractConst();
        auto cleaned_data = ZeroArtifactChannels(norm_data, artifact_channel_mask).ExtractConst();
        RC_DEBOUT(RC::RStr::Join(*artifact_channel_mask, ", ") + "\n");
        cleaned_data->Print(2, 10);

        callback(cleaned_data, task_classifier_settings);
        break;
      }
      default: Throw_RC_Error("Invalid classification type received.");
    } 
  }

  /// Handler that sets the callback on the feature generator results
  /** @param The callback on the classifier results
   */
  void FeatureFilters::SetCallback_Handler(const FeatureCallback &new_callback) {
    callback = new_callback;  
  }
}
