using namespace cli;

#include <QDialog>

class RtfPopup : public QDialog
{
  Q_OBJECT

public:
  RtfPopup(System::String^ rtfText, QWidget* parent = nullptr, Qt::WindowFlags f = Qt::WindowFlags());
};
