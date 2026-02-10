#include <QtCore/QtGlobal>

#if defined(BSAPACKER_LIBRARY)
#	define BSAPACKER_EXPORT Q_DECL_EXPORT
#else
#	define BSAPACKER_EXPORT Q_DECL_IMPORT
#endif
