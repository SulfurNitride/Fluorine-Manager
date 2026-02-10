#ifdef _MSC_VER
#pragma warning(disable : 4251)  // neds to have dll-interface
#pragma warning(disable : 4355)  // this used in initializer list
#pragma warning(disable : 4371)  // layout may have changed
#pragma warning(disable : 4514)  // unreferenced inline function removed
#pragma warning(disable : 4571)  // catch semantics changed
#pragma warning(disable : 4619)  // no warning X
#pragma warning(disable : 4623)  // default constructor deleted
#pragma warning(disable : 4625)  // copy constructor deleted
#pragma warning(disable : 4626)  // copy assignment operator deleted
#pragma warning(disable : 4710)  // function not inlined
#pragma warning(disable : 4820)  // padding
#pragma warning(disable : 4866)  // left-to-right evaluation order
#pragma warning(disable : 4868)  // left-to-right evaluation order
#pragma warning(disable : 5026)  // move constructor deleted
#pragma warning(disable : 5027)  // move assignment operator deleted
#pragma warning(disable : 5045)  // spectre mitigation

#pragma warning(push, 3)
#pragma warning(disable : 4242)  // uint -> const quint8
#pragma warning(disable : 4365)  // signed/unsigned mismatch
#pragma warning(disable : 4668)  // preprocessor macro used but not defined
#pragma warning(disable : 4774)  // bad format string
#pragma warning(disable : 4946)  // reinterpret_cast used between related classes
#pragma warning(disable : 4800)  // implicit conversion
#pragma warning(disable : 5219)  // implicit int -> float conversion
#pragma warning(                                                                       \
    disable : 5249)  // named enumerators with values outside of bit field width
#endif // _MSC_VER

#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING 1

// std
#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <filesystem>
#include <functional>
#include <iostream>
#include <list>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdlib.h>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>
#include <wchar.h>

// windows
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_MEAN_AND_LEAN
#define WIN32_MEAN_AND_LEAN
#endif
#include <ShlObj.h>
#include <Windows.h>
#include <shobjidl.h>
#endif // _WIN32

// Qt
#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QBitmap>
#include <QBuffer>
#include <QCommandLinkButton>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDropEvent>
#include <QFile>
#include <QFileInfo>
#include <QFlags>
#include <QGraphicsObject>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QMainWindow>
#include <QMenuBar>
#include <QMessageBox>
#include <QMetaEnum>
#include <QMetaMethod>
#include <QMetaObject>
#include <QMetaType>
#include <QMouseEvent>
#include <QMutex>
#include <QMutexLocker>
#include <QNetworkReply>
#include <QObject>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWidget>
#include <QRect>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScreen>
#include <QSettings>
#include <QShortcutEvent>
#include <QShowEvent>
#include <QString>
#include <QStyle>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryFile>
#include <QTextEdit>
#include <QTextStream>
#include <QTime>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <QVariant>
#include <QVariantMap>
#include <QVersionNumber>
#include <QWidget>
#include <QtDebug>
#include <qmetaobject.h>

#undef _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING
#ifdef _MSC_VER
#pragma warning(pop)
#endif
