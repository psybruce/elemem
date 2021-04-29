#ifndef EDFSAVE_H
#define EDFSAVE_H

#include "EEGFileSave.h"
#include "EEGData.h"
#include "RC/File.h"
#include "RC/Ptr.h"
#include "RCqt/Worker.h"

namespace CML {
  class Handler;

  class EDFSave : public EEGFileSave {
    public:
    EDFSave(RC::Ptr<Handler> hndl, size_t sampling_rate)
      : EEGFileSave(hndl), buffer(sampling_rate),
        sampling_rate(sampling_rate) {
      callback_ID = RC::RStr("EDFSave_") + RC::RStr(sampling_rate);
      buffer.sampling_rate = sampling_rate;
    }

    RC::RStr GetExt() const { return "edf"; }

    protected:
    void StartFile_Handler(const RC::RStr& filename) override;
    // Thread ordering constraint:
    // Must call Stop after Start, before this destructor, and before
    // hndl->eeg_acq is deleted.
    void StopSaving_Handler() override;
    void SaveData_Handler(RC::APtr<const EEGData>& data) override;

    template<class F, class P>
    void SetChanParam(F func, P p, RC::RStr error_msg);

    int edf_hdl = -1;
    RC::Data1D<uint8_t> channels;
    EEGData buffer;
    size_t amount_buffered = 0;
    size_t amount_written = 0;
    size_t sampling_rate;
    RC::RStr callback_ID;
  };
}

#endif // EDFSAVE_H

