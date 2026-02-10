#pragma once

#include <QObject>
#include <QWidget>
#include <QProgressDialog>

#include "../QObject_unique_ptr.h"

using namespace cli;

class CodeProgressHelper : public QObject {
  Q_OBJECT
public:

  CodeProgressHelper(QWidget* parentWidget);

  void ShowProgressDialog();
  void UpdateProgressValue(int percentage);
  void HideProgressDialog();

public slots:
  void ShowProgressDialogSlot();
  void UpdateProgressValueSlot(int percentage);
  void HideProgressDialogSlot();

signals:

  void ShowProgressDialogSignal();
  void UpdateProgressValueSignal(int percentage);
  void HideProgressDialogSignal();

private:
  QWidget* mParentWidget;
  QObject_unique_ptr<QProgressDialog> mProgressDialog;
};

ref class CodeProgress : OMODFramework::ICodeProgress
{
public:

  CodeProgress(QWidget* parentWidget) : mParentWidget(parentWidget) { }

  virtual void Init(__int64 totalSize, bool compressing);

  virtual void SetProgress(__int64 inSize, __int64 outSize);

  ~CodeProgress();

  !CodeProgress();

  void setParentWidget(QWidget* parentWidget);

private:
  QWidget* mParentWidget;
  CodeProgressHelper* mHelper;
  __int64 mTotalSize;
  int mPercentage;
  bool mCompressing;
};
