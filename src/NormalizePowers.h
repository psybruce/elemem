#ifndef NORMALIZEPOWERS_H
#define NORMALIZEPOWERS_H

#include "EEGPowers.h"
#include "RollingStats.h"
#include "RC/Data2D.h"
#include "RC/Ptr.h"
#include "RCqt/Worker.h"

namespace CML {
  class NormalizePowers {
    public:
    NormalizePowers(size_t eventlen, size_t chanlen, size_t freqlen);

    void Reset();
    void Update(RC::APtr<const EEGPowers>& new_data);
    RC::APtr<EEGPowers> ZScore(RC::APtr<const EEGPowers>& in_data);
    //Data2D<StatsData> GetStats();
	void PrintStats();


	protected:
	RC::Data2D<RollingStats *> rolling_powers;
  };
}

#endif // NORMALIZEPOWERS_H