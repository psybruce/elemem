#ifndef ROLLINGSTATS_H
#define ROLLINGSTATS_H

#include "RC/Data1D.h"

namespace CML {
  class StatsData {
    public:
    RC::Data1D<double> means = {};
    RC::Data1D<double> std_devs = {};
    RC::Data1D<double> sample_std_devs = {};
    StatsData(size_t num_values)
      : means(num_values), std_devs(num_values), sample_std_devs(num_values) {}
    StatsData(RC::Data1D<double> means, RC::Data1D<double> std_devs, 
              RC::Data1D<double> sample_std_devs)
      : means(means), std_devs(std_devs), sample_std_devs(sample_std_devs) {}
  };

  class RollingStats {
    public:
    RollingStats(size_t num_values);

    void Reset();
    void Update(const RC::Data1D<double>& new_values);
    RC::Data1D<double> ZScore(const RC::Data1D<double>& data);
    StatsData GetStats();
	void PrintStats();


    protected:
    int count;
    RC::Data1D<double> means;
    RC::Data1D<double> m2s;
  };
}

#endif // ROLLINGSTATS_H
