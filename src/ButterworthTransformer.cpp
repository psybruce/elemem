#include "ButterworthTransformer.h"
#include "MorletWaveletTransformMP.h"

namespace CML {
  ButterworthTransformer::ButterworthTransformer() {}

  void ButterworthTransformer::Setup(const ButterworthSettings& butterworth_settings) {
    // TODO: JPB: Impl Butterworth Setup
    but_set = butterworth_settings;
  }

  RC::APtr<const EEGData> ButterworthTransformer::Filter(RC::APtr<const EEGData>& data, double freq) {
    // TODO: JPB: Impl Butterworth Filter
    return data;
  } 
  
  RC::APtr<const EEGData> ButterworthTransformer::Filter(RC::APtr<const EEGData>& data, double high_freq, double low_freq) {
    // TODO: JPB: Impl Butterworth Filter
    return data;
  } 
}
