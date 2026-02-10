#include <QApplication>

#include "CodeProgress.h"

CodeProgressHelper::CodeProgressHelper(QWidget* parentWidget) : mParentWidget{ parentWidget }, mProgressDialog(make_nullptr<QProgressDialog>()) {
  moveToThread(QApplication::instance()->thread());
  connect(this, &CodeProgressHelper::ShowProgressDialogSignal, this, &CodeProgressHelper::ShowProgressDialogSlot, Qt::QueuedConnection);
  connect(this, &CodeProgressHelper::UpdateProgressValueSignal, this, &CodeProgressHelper::UpdateProgressValueSlot, Qt::BlockingQueuedConnection);
  connect(this, &CodeProgressHelper::HideProgressDialogSignal, this, &CodeProgressHelper::HideProgressDialogSlot, Qt::QueuedConnection);

}

void CodeProgressHelper::ShowProgressDialog() {
  emit ShowProgressDialogSignal();
}

void CodeProgressHelper::UpdateProgressValue(int percentage) {
  emit UpdateProgressValueSignal(percentage);
}

void CodeProgressHelper::HideProgressDialog() {
  emit HideProgressDialogSignal();
}

void CodeProgressHelper::ShowProgressDialogSlot() {
  mProgressDialog.reset(new QProgressDialog(mParentWidget));
  mProgressDialog->setWindowFlags(mProgressDialog->windowFlags() & ~Qt::WindowContextHelpButtonHint & ~Qt::WindowCloseButtonHint);
  mProgressDialog->setWindowModality(Qt::WindowModal);
  mProgressDialog->setCancelButton(nullptr);
  mProgressDialog->setMinimum(0);
  mProgressDialog->setMaximum(100);
  mProgressDialog->setAutoReset(false);
  mProgressDialog->setAutoClose(false);
  mProgressDialog->show();
}

void CodeProgressHelper::UpdateProgressValueSlot(int percentage) {
  mProgressDialog->setValue(percentage);
}

void CodeProgressHelper::HideProgressDialogSlot() {
  mProgressDialog->hide();
  mProgressDialog.reset();
 }

void CodeProgress::Init(__int64 totalSize, bool compressing)
{
  System::GC::ReRegisterForFinalize(this);

  mTotalSize = totalSize;
  mCompressing = compressing;
  mPercentage = 0;

  mHelper = new CodeProgressHelper(mParentWidget);
  mHelper->ShowProgressDialog();
}

void CodeProgress::SetProgress(__int64 inSize, __int64 outSize)
{
  int newPercentage = (int)(100 * inSize / (double)mTotalSize);
  if (newPercentage != mPercentage)
    mHelper->UpdateProgressValue(newPercentage);
  mPercentage = newPercentage;
}

CodeProgress::~CodeProgress()
{
  if (!mHelper)
    return;

  mHelper->HideProgressDialog();
  this->!CodeProgress();
}

CodeProgress::!CodeProgress()
{
  if (mHelper)
    mHelper->deleteLater();
  mHelper = nullptr;
}

void CodeProgress::setParentWidget(QWidget* parentWidget)
{
  mParentWidget = parentWidget;
}
