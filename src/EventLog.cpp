#include "EventLog.h"

namespace CML {
  void EventLog::StartFile_Handler(const RC::RStr& filename) {
    fw = RC::FileWrite(filename);
  }

  void EventLog::Log_Handler(const RC::RStr& event) {
    if (fw.IsOpen()) {
      fw.Put(event);
      if (last_flush.SinceStart() > 5) {
        fw.Flush();
        last_flush.Start();
      }
    }
  }

  void EventLog::CloseFile_Handler() {
    fw.Close();
  }
}

