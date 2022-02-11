#include "Handler.h"
#include "MainWindow.h"
#include "RC/APtr.h"
#include "RC/Data1D.h"
#include "RC/RStr.h"
#include "RC/Errors.h"
#include "RCQApplication.h"
#include "QtStyle.h"

#include <iostream>
#include <stdlib.h>
#include <QtGui>

int main (int argc, char *argv[]) {
  int retval = -1;

  RC::Segfault::SetHandler();

  try {
    RCQApplication app(argc, argv);
    app.setStyleSheet(Resource::QtStyle_css.c_str());

    RC::APtr<CML::Handler> hndl(new CML::Handler());

    CML::MainWindow main_window(hndl);

    {
      RCqt::Worker::DirectCallingScope direct;

      hndl->SetMainWindow(&main_window);

      hndl->LoadSysConfig();  // Must come before RegisterEEGDisplay.
      hndl->Initialize();
    }

    main_window.RegisterEEGDisplay();
    main_window.show();

    retval = app.exec();
  }
  catch (RC::ErrorMsgFatal& err) {
    std::cerr << "Fatal Error:  " << err.what() << std::endl;
    retval = -1;
    exit(-1);
  }
  catch (RC::ErrorMsg& err) {
    std::cerr << "Error:  " << err.what() << std::endl;
    retval = -2;
  }
  catch (std::exception &ex) {
    std::cerr << "Unhandled exception: " << ex.what() << std::endl;
    retval = -3;
  }

  return retval;
}

