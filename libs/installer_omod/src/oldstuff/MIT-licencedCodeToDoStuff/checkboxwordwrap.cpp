#include "checkboxwordwrap.h"

#include <QStyle>
#include <QStyleOptionButton>

CheckBoxWordWrap::CheckBoxWordWrap(QWidget *parent)
    : QCheckBox (parent)
    , m_hMainLayout(new QHBoxLayout(this))
    , m_label(new ClickableLabel(this))
{
    init();
}

CheckBoxWordWrap::CheckBoxWordWrap(const QString &text, QWidget *parent)
    : QCheckBox (parent)
    , m_hMainLayout(new QHBoxLayout(this))
    , m_label(new ClickableLabel(text, this))
{
    init();
}

CheckBoxWordWrap::~CheckBoxWordWrap()
{
    delete m_label;
    delete m_hMainLayout;
}

bool CheckBoxWordWrap::isWordWrap() const
{
    return m_label->wordWrap();
}

void CheckBoxWordWrap::setWordWrap(bool wordwrap)
{
    m_label->setWordWrap(wordwrap);
}

QString CheckBoxWordWrap::text() const
{
    return m_label->text();
}

void CheckBoxWordWrap::setText(const QString &text)
{
    m_label->setText(text);
}

QSize CheckBoxWordWrap::sizeHint() const
{
    QFontMetrics fm(m_label->font());
    QRect r = m_label->rect();
    r.setLeft(r.left()+m_label->indent()+separation);
    QRect bRect = fm.boundingRect(r, int(Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap), m_label->text());
    QSize ret = QSize(QWidget::sizeHint().width(), bRect.height());
    return ret;
}

void CheckBoxWordWrap::labelIsClicked()
{
    setChecked(!isChecked());
}

void CheckBoxWordWrap::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateGeometry();
}

void CheckBoxWordWrap::init()
{
    setLayout(m_hMainLayout);
    QStyleOptionButton opt;
    initStyleOption(&opt);
    int indicatorW = style()->pixelMetric(QStyle::PixelMetric::PM_IndicatorWidth, &opt, this);
    // Useless in our case, we only need the indicator width
    //int indicatorH = style()->pixelMetric(QStyle::PixelMetric::PM_IndicatorHeight, &opt, this);
    m_hMainLayout->setContentsMargins(0, 0, 0, 0);
    m_hMainLayout->addWidget(m_label);
    m_label->setIndent(indicatorW+separation);
    m_label->setWordWrap(true);

    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    connect(m_label, SIGNAL(clicked()), this, SLOT(labelIsClicked()));
}


RadioButtonWordWrap::RadioButtonWordWrap(QWidget* parent)
  : QRadioButton(parent)
  , m_hMainLayout(new QHBoxLayout(this))
  , m_label(new ClickableLabel(this))
{
  init();
}

RadioButtonWordWrap::RadioButtonWordWrap(const QString& text, QWidget* parent)
  : QRadioButton(parent)
  , m_hMainLayout(new QHBoxLayout(this))
  , m_label(new ClickableLabel(text, this))
{
  init();
}

RadioButtonWordWrap::~RadioButtonWordWrap()
{
  delete m_label;
  delete m_hMainLayout;
}

bool RadioButtonWordWrap::isWordWrap() const
{
  return m_label->wordWrap();
}

void RadioButtonWordWrap::setWordWrap(bool wordwrap)
{
  m_label->setWordWrap(wordwrap);
}

QString RadioButtonWordWrap::text() const
{
  return m_label->text();
}

void RadioButtonWordWrap::setText(const QString& text)
{
  m_label->setText(text);
}

QSize RadioButtonWordWrap::sizeHint() const
{
  QFontMetrics fm(m_label->font());
  QRect r = m_label->rect();
  r.setLeft(r.left() + m_label->indent() + separation);
  QRect bRect = fm.boundingRect(r, int(Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap), m_label->text());
  QSize ret = QSize(QWidget::sizeHint().width(), bRect.height());
  return ret;
}

void RadioButtonWordWrap::labelIsClicked()
{
  setChecked(!isChecked());
}

void RadioButtonWordWrap::resizeEvent(QResizeEvent* event)
{
  QWidget::resizeEvent(event);
  updateGeometry();
}

void RadioButtonWordWrap::init()
{
  setLayout(m_hMainLayout);
  QStyleOptionButton opt;
  initStyleOption(&opt);
  int indicatorW = style()->pixelMetric(QStyle::PixelMetric::PM_IndicatorWidth, &opt, this);
  // Useless in our case, we only need the indicator width
  //int indicatorH = style()->pixelMetric(QStyle::PixelMetric::PM_IndicatorHeight, &opt, this);
  m_hMainLayout->setContentsMargins(0, 0, 0, 0);
  m_hMainLayout->addWidget(m_label);
  m_label->setIndent(indicatorW + separation);
  m_label->setWordWrap(true);

  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
  connect(m_label, SIGNAL(clicked()), this, SLOT(labelIsClicked()));
}
