#include "DialogSelect.h"

#include <functional>

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDebug>
#include <QImageReader>
#include <QPlainTextEdit>
#include <QRadioButton>
#include <QScrollArea>
#include <QSplitter>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "MIT-licencedCodeToDoStuff/checkboxwordwrap.h"

// there is no hover signal, the only way to know is to override enterEvent()
//
template <class T, typename... Args>
class HoverableWidget : public T
{
public:
  std::function<void ()> onHover;

  HoverableWidget(Args... args, std::function<void ()> h)
    : T(args...), onHover(std::move(h))
  {
  }

protected:  void enterEvent(QEnterEvent*) override
  {
    onHover();
  }
};


FixedAspectRatioImageLabel::FixedAspectRatioImageLabel(QWidget* parent) : QLabel(parent)
{
}

void FixedAspectRatioImageLabel::setUnscaledPixmap(const QPixmap& pixmap)
{
  mUnscaledPixmap = pixmap;
  setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
  rescalePixmap(size());

  copySizeLabel.setPixmap(pixmap);
}

const QPixmap& FixedAspectRatioImageLabel::unscaledPixmap() const
{
    return mUnscaledPixmap;
}

QSize FixedAspectRatioImageLabel::sizeHint() const
{
  if (mUnscaledPixmap.isNull())
  {
    return QLabel::sizeHint();
  }
  else
  {
    // maybe should add frame border
    return mUnscaledPixmap.size();
    //return copySizeLabel.sizeHint();
  }
}

bool FixedAspectRatioImageLabel::hasHeightForWidth() const
{
  return true;
}

int FixedAspectRatioImageLabel::heightForWidth(int width) const
{
  // this ignores the difference between size and contentsRect size
  return mUnscaledPixmap.height() * width / (double)mUnscaledPixmap.width();
}

int FixedAspectRatioImageLabel::widthForHeight(int height) const
{
  return mUnscaledPixmap.width() * height / (double)mUnscaledPixmap.height();
}

void FixedAspectRatioImageLabel::resizeEvent(QResizeEvent* resizeEvent)
{
  QLabel::resizeEvent(resizeEvent);
  rescalePixmap(contentsRect().size());
}

void FixedAspectRatioImageLabel::rescalePixmap(const QSize& size)
{
  setPixmap(mUnscaledPixmap.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}


std::optional<QVector<int>> DialogSelect(
  QWidget* parent, const QString& title, const QVector<QString>& items,
  const QVector<QString>& descriptions, const QVector<QString>& pixmaps,
  bool multiSelect)
{
  QDialog d(parent);
  d.setWindowTitle(title);

  auto* mainLayout = new QVBoxLayout(&d);
  d.setLayout(mainLayout);

  auto* splitter = new QSplitter(&d);
  mainLayout->addWidget(splitter);

  // button box
  auto* buttons = new QDialogButtonBox(
    QDialogButtonBox::Ok|QDialogButtonBox::Cancel, &d);

  /* QObject::connect: signal not found in QDialogButtonBox
     even though this worked with my initial attempt.
     Holt had the same issue: https://stackoverflow.com/questions/61879664/qobjectconnect-not-working-signal-not-found-with-function-syntax
     The new syntax is much better, but as this code wants replacing, that's not a hill I'm going to die on.
  QObject::connect(buttons, &QDialogButtonBox::accepted, [&] {
    d.accept();
  });

  QObject::connect(buttons, &QDialogButtonBox::rejected, [&] {
    d.reject();
  });
  */
  QObject::connect(buttons, SIGNAL(accepted()), &d, SLOT(accept()));
  QObject::connect(buttons, SIGNAL(rejected()), &d, SLOT(reject()));

  mainLayout->addWidget(buttons);


  // left panel
  auto* left = new QWidget(splitter);
  auto* leftLayout = new QVBoxLayout(left);
  leftLayout->setContentsMargins(0, 0, 0, 0);

  auto* stack = new QStackedWidget(left);
  leftLayout->addWidget(stack);

  // don't put descriptions of pixmaps at all if there aren't any
  const bool hasDescriptions = !descriptions.empty();
  const bool hasPixmaps = !pixmaps.empty();

  // for each description/item
  for (int i=0; i < std::max(descriptions.size(), pixmaps.size()); ++i) {
    auto* panel = new QWidget(left);
    auto* panelLayout = new QVBoxLayout(panel);
    //panelLayout->setContentsMargins(0, 0, 0, 0);

    if (hasPixmaps) {
      auto* pixmapLabel = new FixedAspectRatioImageLabel(panel);

      pixmapLabel->setFrameStyle(QFrame::StyledPanel);
      pixmapLabel->setLineWidth(1);

      // make it resizable
      //pixmapLabel->setMinimumSize(1, 150);
      //pixmapLabel->setScaledContents(true);
      //pixmapLabel->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);

      if (i < pixmaps.size()) {
        // this item has a pixmap
        // Use QImageReader as (despite what the documentation says) QPixmap uses the extension, rather than the file header, to determine the format.
        QImageReader reader(pixmaps[i]);
        reader.setDecideFormatFromContent(true);
        QImage image = reader.read();
        if (image.isNull()) {
          pixmapLabel->setText(QString("failed to load '%1': %2 (code %3)").arg(pixmaps[i], reader.errorString(), QString::number(reader.error())));
        } else {
          QPixmap pixmap = QPixmap::fromImage(image);
          pixmapLabel->setUnscaledPixmap(std::move(pixmap));
        }
      }

      panelLayout->addWidget(pixmapLabel);
    }

    if (hasDescriptions)
    {
      // description under pixmap (if any)
      auto* description = new QPlainTextEdit(descriptions[i], panel);

      // this puts the description on top if there's no pixmap
      //description->setAlignment(Qt::AlignLeft|Qt::AlignTop);

      //description->setWordWrap(true);

      panelLayout->addWidget(description);
    }

    // if the pixmap is first, it'll take as much space as it can; if the
    // description is first, the AlignTop makes it work anyways
    panelLayout->setStretch(0, 1);

    stack->addWidget(panel);
  }


  // right panel
  auto* right = new QWidget(&d);
  auto* rightOuterLayout = new QVBoxLayout(right);
  rightOuterLayout->setContentsMargins(0, 0, 0, 0);

  // title
  auto* titleLabel = new QLabel(title, right);
  titleLabel->setWordWrap(true);
  rightOuterLayout->addWidget(titleLabel);

  QWidget* rightInner;
  QBoxLayout* rightInnerLayout;

  // Doing this unconditionally breaks HGEC lower body choices.
  // Somehow the fixed aspect ratio image label forces the scroll area to be resized below its preferred size.
  // Because the word wrapped radio buttons aren't good at telling Qt their size hint is only minimal for the current width,
  // the layout ends up with their preferred width as its minimum and won't let itself be shrunk smaller than that even though you can't see the right hand side.
  // This means that as they're in a widget with enough space, the text isn't wrapped.
  // The exact same behaviour happens with non-wrappable radio buttons, but it's more expected.
  // Making the fixed aspect ratio image label play nicely with being shrunk will probably fix this.
  // It only seems to be MEAT that actually needs a scrollbar, though, and that looks fine, so fixing this mess is left as an exercise for the reader.
  if (items.size() >= 10)
  {
    QScrollArea* rightScrollArea = new QScrollArea(right);
    rightOuterLayout->addWidget(rightScrollArea);
    rightScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    rightScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    rightInner = new QWidget(right);
    rightScrollArea->setWidget(rightInner);
    rightScrollArea->setWidgetResizable(true);

    rightInnerLayout = new QVBoxLayout(rightInner);
  }
  else
  {
    rightInner = right;
    rightInnerLayout = rightOuterLayout;
  }

  // callback when hovering
  auto onHover = [&](int i) {
    stack->setCurrentIndex(i);
  };


  // remember buttons to see which were checked
  QVector<QAbstractButton*> itemButtons;

  // for each item
  for (int i=0; i<items.size(); ++i) {
    QAbstractButton* w = nullptr;

    QString labelText = items[i];
    bool checked = false;

    // THIS IS IMPORTANT WHEN REPLACING THIS WITH BETTER CODE. OMODS HAVE A PIPE AT THE START OF ANY OPTIONS SELECTED BY DEFAULT.
    if (labelText.startsWith('|'))
    {
      labelText = labelText.remove(0, 1);
      checked = true;
    }

    if (multiSelect) {
      w = new HoverableWidget<CheckBoxWordWrap, const QString&, QWidget*>(labelText, rightInner, [=]{ onHover(i); });
    } else {
      w = new HoverableWidget<RadioButtonWordWrap, const QString&, QWidget*>(labelText, rightInner, [=]{ onHover(i); });
      checked |= i == 0;
    }

    rightInnerLayout->addWidget(w);
    itemButtons.push_back(w);
    w->setChecked(checked);
  }

  // push all the items to the top
  rightInnerLayout->addStretch(1);

  splitter->addWidget(left);
  splitter->addWidget(right);

  // decent initial size
  d.resize(800, 500);

  if (d.exec() != QDialog::Accepted) {
    return std::nullopt;
  }


  // go through every button, it doesn't matter whether they're checkboxes or
  // radio buttons
  QVector<int> selection;
  for (int i=0; i<itemButtons.size(); ++i) {
    if (itemButtons[i]->isChecked()) {
      selection.push_back(i);
    }
  }

  return selection;
}
