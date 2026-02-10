#include "morrowindsavegameinfowidget.h"
#include "ui_morrowindsavegameinfowidget.h"

#include "gamemorrowind.h"
#include "imoinfo.h"
#include "ipluginlist.h"
#include "morrowindsavegame.h"
#include "morrowindsavegameinfo.h"

#include <QDate>
#include <QDateTime>
#include <QFont>
#include <QFrame>
#include <QLabel>
#include <QLayout>
#include <QLayoutItem>
#include <QPixmap>
#include <QString>
#include <QStyle>
#include <QTime>
#include <QVBoxLayout>

#include <Qt>
#include <QtGlobal>

#include <memory>

MorrowindSaveGameInfoWidget::MorrowindSaveGameInfoWidget(
    MorrowindSaveGameInfo const* info, QWidget* parent)
    : MOBase::ISaveGameInfoWidget(parent), ui(new Ui::MorrowindSaveGameInfoWidget),
      m_Info(info)
{
  ui->setupUi(this);
  this->setWindowFlags(Qt::ToolTip | Qt::BypassGraphicsProxyWidget);
  setWindowOpacity(style()->styleHint(QStyle::SH_ToolTipLabel_Opacity, 0, this) /
                   qreal(255.0));
  ui->gameFrame->setStyleSheet("background-color: transparent;");

  QVBoxLayout* gameLayout = new QVBoxLayout();
  gameLayout->setContentsMargins(0, 0, 0, 0);
  gameLayout->setSpacing(2);
  ui->gameFrame->setLayout(gameLayout);
}

MorrowindSaveGameInfoWidget::~MorrowindSaveGameInfoWidget()
{
  delete ui;
}

void MorrowindSaveGameInfoWidget::setSave(MOBase::ISaveGame const& save)
{
  auto const& morrowindSave = dynamic_cast<MorrowindSaveGame const&>(save);

  ui->saveNameLabel->setText(QString("%1 (Day %2)")
                                 .arg(morrowindSave.getSaveName())
                                 .arg(morrowindSave.getGameDays()));
  ui->saveNumLabel->setText(QString("%1").arg(morrowindSave.getSaveNumber()));
  ui->healthLabel->setText(QString("%1 / %2")
                               .arg(round(morrowindSave.getPCCurrentHealth()))
                               .arg(morrowindSave.getPCMaxHealth()));
  ui->characterLabel->setText(morrowindSave.getPCName());
  ui->locationLabel->setText(morrowindSave.getPCLocation());
  ui->levelLabel->setText(QString("%1").arg(morrowindSave.getPCLevel()));

  // This somewhat contorted code is because on my system at least, the
  // old way of doing this appears to give short date and long time.
  QDateTime t = morrowindSave.getCreationTime();
  ui->dateLabel->setText(
      QLocale::system().toString(t.date(), QLocale::FormatType::ShortFormat) + " " +
      QLocale::system().toString(t.time(), QLocale::FormatType::ShortFormat));
  ui->screenshotLabel->setPixmap(QPixmap::fromImage(morrowindSave.getScreenshot()));
  if (ui->gameFrame->layout() != nullptr) {
    QLayoutItem* item = nullptr;
    while ((item = ui->gameFrame->layout()->takeAt(0)) != nullptr) {
      delete item->widget();
      delete item;
    }
    ui->gameFrame->layout()->setSizeConstraint(QLayout::SetFixedSize);
  }

  // Resize box to new content
  this->resize(0, 0);

  QLayout* layout   = ui->gameFrame->layout();
  QLabel* header    = new QLabel(tr("Missing ESPs"));
  QFont headerFont  = header->font();
  QFont contentFont = headerFont;
  headerFont.setItalic(true);
  contentFont.setBold(true);
  contentFont.setPointSize(7);
  header->setFont(headerFont);
  layout->addWidget(header);
  int count                       = 0;
  MOBase::IPluginList* pluginList = m_Info->m_Game->m_Organizer->pluginList();
  for (QString const& pluginName : morrowindSave.getPlugins()) {
    if (pluginList->state(pluginName) == MOBase::IPluginList::STATE_ACTIVE) {
      continue;
    }

    ++count;

    if (count > 7) {
      break;
    }

    QLabel* pluginLabel = new QLabel(pluginName);
    pluginLabel->setIndent(10);
    pluginLabel->setFont(contentFont);
    layout->addWidget(pluginLabel);
  }
  if (count > 7) {
    QLabel* dotDotLabel = new QLabel("...");
    dotDotLabel->setIndent(10);
    dotDotLabel->setFont(contentFont);
    layout->addWidget(dotDotLabel);
  }
  if (count == 0) {
    QLabel* dotDotLabel = new QLabel(tr("None"));
    dotDotLabel->setIndent(10);
    dotDotLabel->setFont(contentFont);
    layout->addWidget(dotDotLabel);
  }
}
