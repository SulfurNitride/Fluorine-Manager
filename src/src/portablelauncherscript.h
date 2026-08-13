#ifndef PORTABLELAUNCHERSCRIPT_H
#define PORTABLELAUNCHERSCRIPT_H

#include <QString>

namespace portable_launcher_script
{

enum class Status
{
  Created,
  Preserved,
  Failed,
};

struct Result
{
  Status status = Status::Failed;
  QString path;
  QString error;
};

// Creates the convenience launcher for a newly authored portable instance.
// Existing filesystem entries are always preserved; imported instances must
// never call this function merely to inspect or register themselves.
Result create(const QString& instanceDirectory);

}

#endif
