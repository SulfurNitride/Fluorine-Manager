#ifndef CHECKBOXWORDWRAP_H
#define CHECKBOXWORDWRAP_H

#include <QCheckBox>
#include <QHBoxLayout>
#include <QRadioButton>

#include "clickablelabel.h"

/**
 * @author thibdev
 */
class CheckBoxWordWrap : public QCheckBox
{
    Q_OBJECT

    Q_PROPERTY(bool wordwrap READ isWordWrap WRITE setWordWrap)
    Q_PROPERTY(QString text READ text WRITE setText)

public:
    CheckBoxWordWrap(QWidget *parent = Q_NULLPTR);
    CheckBoxWordWrap(const QString &text, QWidget *parent = Q_NULLPTR);
    ~CheckBoxWordWrap();
    bool isWordWrap() const;
    void setWordWrap(bool wordwrap);
    QString text() const;
    void setText(const QString &text);
    QSize sizeHint() const override;

private slots:
    void labelIsClicked();

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void init();
    const int separation = 5;
    QHBoxLayout     *m_hMainLayout;
    ClickableLabel  *m_label;

};

class RadioButtonWordWrap : public QRadioButton
{
  Q_OBJECT

    Q_PROPERTY(bool wordwrap READ isWordWrap WRITE setWordWrap)
    Q_PROPERTY(QString text READ text WRITE setText)

public:
  RadioButtonWordWrap(QWidget* parent = Q_NULLPTR);
  RadioButtonWordWrap(const QString& text, QWidget* parent = Q_NULLPTR);
  ~RadioButtonWordWrap();
  bool isWordWrap() const;
  void setWordWrap(bool wordwrap);
  QString text() const;
  void setText(const QString& text);
  QSize sizeHint() const override;

private slots:
  void labelIsClicked();

protected:
  void resizeEvent(QResizeEvent* event) override;

private:
  void init();
  const int separation = 5;
  QHBoxLayout* m_hMainLayout;
  ClickableLabel* m_label;

};

#endif
