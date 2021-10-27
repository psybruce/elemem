#ifndef CLASSIFIER_H
#define CLASSIFIER_H

#include "EEGData.h"
#include "RC/APtr.h"
#include "RCqt/Worker.h"

namespace CML {
  using MorletCallback = RCqt::TaskCaller<RC::APtr<const RC::Data1D<double>>>;
  using ClassifierCallback = RCqt::TaskCaller<bool>;

  class Classifier : public RCqt::WorkerThread {
    public:
    virtual ~Classifier() {}

    MorletCallback Classify =
      TaskHandler(Classifier::Classifier_Handler);

    RCqt::TaskCaller<ClassifierCallback> SetCallback =
      TaskHandler(Classifier::SetCallback_Handler);

    protected:
    virtual void Classifier_Handler(RC::APtr<const RC::Data1D<double>>&) = 0;

    /// Sets the callback for the classification result
    /** @param new_callback The new callback to be set
     */
    void SetCallback_Handler(ClassifierCallback& new_callback) { callback = new_callback; }

    ClassifierCallback callback;
  };
}

#endif // CLASSIFIER_EVEN_ODD_H
