#ifndef NXMHANDLER_LINUX_H
#define NXMHANDLER_LINUX_H

#include "nxmhandlerintegration.h"

class NxmHandlerLinux
{
public:
  static nxm_handler_integration::Paths paths();
  static bool recognizesCompleteRegistration();
  static nxm_handler_integration::Result registerHandler(bool forceDefault);
  static nxm_handler_integration::Result unregisterHandler();
};

#endif  // NXMHANDLER_LINUX_H
