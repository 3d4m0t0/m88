#include "config_dialog.h"
#include "qt_platform.h"

#include "../linux/linux_config.h"
#include "../linux/linux_ime.h"
#include "../linux/m88_wayland_idle_inhibit.h"
#include "../linux/m88_miniaudio_devices.h"
#include "../common/misc.h"

#include <QApplication>
#include <QCoreApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QFont>
#include <QLabel>
#include <QHBoxLayout>
#include <QAbstractButton>
#include <QSizePolicy>
#include <QEvent>
#include <QHelpEvent>
#include <QPushButton>
#include <QRadioButton>
#include <QTabBar>
#include <QToolTip>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QWidget>

using PC8801::Config;

#include <cstring>

namespace {

#define CFG_TR(source) QCoreApplication::translate("ConfigDialog", source)

void PopulateSoundBackendCombo(QComboBox* combo) {
  if (!combo) {
    return;
  }
  combo->clear();
  combo->addItem(CFG_TR("Auto"), QString());
  combo->addItem(QStringLiteral("PulseAudio"), QStringLiteral("pulse"));
  combo->addItem(QStringLiteral("ALSA"), QStringLiteral("alsa"));
  combo->addItem(QStringLiteral("JACK"), QStringLiteral("jack"));
}

void PopulateSoundDeviceCombo(QComboBox* combo, const char* backend_name,
                              const QString& select_device = {}) {
  if (!combo) {
    return;
  }
  const bool auto_backend = M88MiniaudioDevices::IsAutoBackend(backend_name);
  combo->setEnabled(!auto_backend);
  if (auto_backend) {
    combo->clear();
    combo->addItem(CFG_TR("Default"), QString());
    combo->setCurrentIndex(0);
    return;
  }
  const QString keep =
      select_device.isNull() ? combo->currentData().toString() : select_device;
  combo->clear();
  combo->addItem(CFG_TR("Default"), QString());
  for (const auto& dev : M88MiniaudioDevices::ListPlayback(backend_name)) {
    const QString display = QString::fromUtf8(dev.name.c_str());
    const QString id =
        dev.id.empty() ? display : QString::fromUtf8(dev.id.c_str());
    combo->addItem(display, id);
  }
  int idx = combo->findData(keep);
  if (idx < 0 && !keep.isEmpty()) {
    // Legacy configs stored the UI description in AudioDevice=.
    for (int i = 1; i < combo->count(); ++i) {
      if (combo->itemText(i) == keep) {
        idx = i;
        break;
      }
    }
  }
  if (idx < 0 && !keep.isEmpty()) {
    combo->addItem(CFG_TR("%1 (not found)").arg(keep), keep);
    idx = combo->count() - 1;
  }
  combo->setCurrentIndex(idx >= 0 ? idx : 0);
}

void PopulateFm44Combo(QComboBox* combo) {
  if (!combo) {
    return;
  }
  combo->clear();
  combo->addItem(CFG_TR("OPN"), 0);
  combo->addItem(CFG_TR("OPNA"), 1);
  combo->addItem(CFG_TR("None"), 2);
}

void PopulateFmA8Combo(QComboBox* combo) {
  if (!combo) {
    return;
  }
  combo->clear();
  combo->addItem(CFG_TR("OPN (MkII)"), 0);
  combo->addItem(CFG_TR("OPNA (MkIII)"), 1);
  combo->addItem(CFG_TR("None"), 2);
}

int Fm44ModeFromConfig(const Config& cfg) {
  if (cfg.flag2 & Config::disableopn44) {
    return 2;
  }
  if (cfg.flags & Config::enableopna) {
    return 1;
  }
  return 0;
}

int FmA8ModeFromConfig(const Config& cfg) {
  if (cfg.flags & Config::opnaona8) {
    return 1;
  }
  if (cfg.flags & Config::opnona8) {
    return 0;
  }
  return 2;
}

void SetComboByData(QComboBox* combo, int data) {
  if (!combo) {
    return;
  }
  const int idx = combo->findData(data);
  combo->setCurrentIndex(idx >= 0 ? idx : 0);
}

int LimitInt(int v, int maxv, int minv) { return Limit(v, maxv, minv); }

QSlider* MakeVolumeSlider(int value, QWidget* parent) {
  auto* slider = new QSlider(Qt::Horizontal, parent);
  slider->setRange(-40, 20);
  slider->setValue(value);
  return slider;
}

void UpdateVolumeLabel(QLabel* label, int val) {
  if (val > -40) {
    label->setText(QString::number(val));
  } else {
    label->setText(CFG_TR("Mute"));
  }
}

constexpr const char kResetTooltipText[] =
    QT_TRANSLATE_NOOP("ConfigDialog",
                      "Requires a machine reset to\n"
                      "take effect. (Control - Reset).");

void CompactVBox(QVBoxLayout* layout) {
  layout->setContentsMargins(4, 4, 4, 4);
  layout->setSpacing(3);
  layout->setAlignment(Qt::AlignTop);
}

void FinishTabPage(QVBoxLayout* layout) {
  layout->addStretch(1);
}

void ShrinkGroupBox(QGroupBox* box) {
  if (!box) {
    return;
  }
  QSizePolicy policy = box->sizePolicy();
  policy.setVerticalPolicy(QSizePolicy::Maximum);
  box->setSizePolicy(policy);
}

void EqualRowGroupBox(QGroupBox* box) {
  if (!box) {
    return;
  }
  QSizePolicy policy = box->sizePolicy();
  policy.setHorizontalPolicy(QSizePolicy::Expanding);
  policy.setVerticalPolicy(QSizePolicy::Expanding);
  box->setSizePolicy(policy);
}

void ShrinkTabPage(QWidget* page) {
  if (!page) {
    return;
  }
  QSizePolicy policy = page->sizePolicy();
  policy.setVerticalPolicy(QSizePolicy::Maximum);
  page->setSizePolicy(policy);
}

void CompactForm(QFormLayout* layout) {
  layout->setContentsMargins(4, 4, 4, 4);
  layout->setVerticalSpacing(4);
  layout->setHorizontalSpacing(8);
}

void CompactGrid(QGridLayout* layout) {
  layout->setContentsMargins(4, 4, 4, 4);
  layout->setVerticalSpacing(2);
  layout->setHorizontalSpacing(6);
}

void CompactGroupBoxLayout(QLayout* layout) {
  layout->setContentsMargins(4, 2, 4, 2);
  layout->setSpacing(1);
}

void SetResetRequiredTooltip(QWidget* widget) {
  if (!widget) {
    return;
  }
  widget->setToolTip(CFG_TR(kResetTooltipText));
  widget->setAttribute(Qt::WA_Hover, true);
  widget->setAttribute(Qt::WA_AlwaysShowToolTips, true);
}

class TooltipEventFilter : public QObject {
 public:
  explicit TooltipEventFilter(QObject* parent = nullptr) : QObject(parent) {}

 protected:
  bool eventFilter(QObject* obj, QEvent* event) override {
    if (event->type() != QEvent::ToolTip) {
      return QObject::eventFilter(obj, event);
    }
    auto* help = static_cast<QHelpEvent*>(event);
    QString tip;
    QWidget* host = nullptr;

    if (auto* bar = qobject_cast<QTabBar*>(obj)) {
      const int idx = bar->tabAt(help->pos());
      if (idx >= 0) {
        tip = bar->tabToolTip(idx);
        host = bar;
      }
    } else if (auto* widget = qobject_cast<QWidget*>(obj)) {
      for (QWidget* w = widget; w && tip.isEmpty(); w = w->parentWidget()) {
        if (auto* bar = qobject_cast<QTabBar*>(w)) {
          const QPoint pos = bar->mapFromGlobal(help->globalPos());
          const int idx = bar->tabAt(pos);
          if (idx >= 0) {
            tip = bar->tabToolTip(idx);
            host = bar;
          }
          break;
        }
        const QString candidate = w->toolTip();
        if (!candidate.isEmpty()) {
          tip = candidate;
          host = w;
        }
      }
    }

    if (!tip.isEmpty() && host) {
      QToolTip::showText(help->globalPos(), tip, host);
      return true;
    }
    return QObject::eventFilter(obj, event);
  }
};

}  // namespace

ConfigDialog::ConfigDialog(Config config, QWidget* parent)
    : QDialog(parent), config_(config) {
  M88GetDefaultConfig(&default_config_, &default_wayland_idle_, &default_ime_kana_);
  setWindowTitle(tr("M88 Configuration"));
  QFont dlg_font = font();
  if (dlg_font.pointSize() > 0) {
    dlg_font.setPointSize(qMax(8, dlg_font.pointSize() - 2));
  } else {
    dlg_font.setPointSize(9);
  }
  setFont(dlg_font);
  if (QApplication* app = qApp) {
    setPalette(app->palette());
  }
  buildUi();
  loadFromConfig();
  setApplyEnabled(false);
  applyResetRequiredTooltips();
  installTooltipDelivery();
  applyFixedDialogSize();
}

void ConfigDialog::applyFixedDialogSize() {
  if (!tabs_) {
    return;
  }
  auto* main = qobject_cast<QVBoxLayout*>(layout());
  if (!main) {
    return;
  }

  QTabBar* bar = tabs_->tabBar();
  const QMargins margins = main->contentsMargins();
  constexpr int kTabWidthFudge = 8;
  const int width = bar->sizeHint().width() + margins.left() + margins.right() + kTabWidthFudge;
  bar->setUsesScrollButtons(false);

  int max_height = 0;
  const int prev_tab = tabs_->currentIndex();
  for (int i = 0; i < tabs_->count(); ++i) {
    tabs_->setCurrentIndex(i);
    if (QWidget* page = tabs_->widget(i)) {
      if (QLayout* page_layout = page->layout()) {
        page_layout->activate();
      }
    }
    max_height = qMax(max_height, sizeHint().height());
  }
  tabs_->setCurrentIndex(prev_tab >= 0 ? prev_tab : 0);

  setFixedSize(width, max_height);
}

void ConfigDialog::buildUi() {
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(6, 6, 6, 6);
  layout->setSpacing(4);
  tabs_ = new QTabWidget(this);
  if (QTabBar* bar = tabs_->tabBar()) {
    bar->setExpanding(false);
    bar->setUsesScrollButtons(false);
  }
  layout->addWidget(tabs_);

  // --- Hardware (CPU, display hardware, sound board) ---
  {
    auto* page = new QWidget(tabs_);
    auto* v = new QVBoxLayout(page);
    CompactVBox(v);

    auto* speed_box = new QGroupBox(tr("Speed"), page);
    auto* speed_layout = new QVBoxLayout(speed_box);
    CompactGroupBoxLayout(speed_layout);
    cpu_clock_mhz_ = new QSpinBox(speed_box);
    cpu_clock_mhz_->setRange(1, 100);
    cpu_clock_mhz_->setSuffix(tr(" MHz"));
    cpu_nowait_ = new QCheckBox(tr("Full speed (&M)"), speed_box);
    cpu_burst_ = new QCheckBox(tr("Skip speed limit (&N)"), speed_box);
    auto* clock_row = new QHBoxLayout();
    clock_row->setSpacing(8);
    clock_row->addWidget(new QLabel(tr("CPU clock (&C):"), speed_box));
    clock_row->addWidget(cpu_clock_mhz_);
    auto* speed_flags = new QWidget(speed_box);
    auto* speed_flags_layout = new QHBoxLayout(speed_flags);
    speed_flags_layout->setContentsMargins(0, 0, 0, 0);
    speed_flags_layout->setSpacing(8);
    speed_flags_layout->addWidget(cpu_nowait_, 1);
    speed_flags_layout->addWidget(cpu_burst_, 1);
    clock_row->addWidget(speed_flags, 1);
    speed_layout->addLayout(clock_row);

    cpu_speed_ = new QSlider(Qt::Horizontal, speed_box);
    cpu_speed_->setRange(2, 20);
    cpu_speed_label_ = new QLabel(speed_box);
    auto* ratio_row = new QHBoxLayout();
    ratio_row->setSpacing(4);
    ratio_row->addWidget(new QLabel(tr("Ratio (&B):"), speed_box));
    ratio_row->addWidget(cpu_speed_, 1);
    ratio_row->addWidget(cpu_speed_label_);
    speed_layout->addLayout(ratio_row);
    v->addWidget(speed_box);

    auto* hw_row = new QHBoxLayout();
    hw_row->setSpacing(4);

    auto* misc_box = new QGroupBox(tr("Misc"), page);
    misc_box->setProperty("hw_equal_row", true);
    auto* misc_layout = new QVBoxLayout(misc_box);
    misc_layout->setSpacing(2);
    cpu_no_subcpu_ctrl_ = new QCheckBox(tr("Run sub CPU always (&S)"), misc_box);
    cpu_enable_wait_ = new QCheckBox(tr("Wait (&W)"), misc_box);
    cpu_fdd_wait_ = new QCheckBox(tr("FDD wait (&F)"), misc_box);
    misc_layout->addWidget(cpu_no_subcpu_ctrl_);
    misc_layout->addWidget(cpu_enable_wait_);
    misc_layout->addWidget(cpu_fdd_wait_);
    misc_layout->addStretch();
    hw_row->addWidget(misc_box, 1);

    auto* ms_box = new QGroupBox(tr("Main/sub CPU ratio (&R)"), page);
    ms_box->setProperty("hw_equal_row", true);
    auto* ms_layout = new QVBoxLayout(ms_box);
    ms_layout->setSpacing(2);
    cpu_ms_group_ = new QButtonGroup(ms_box);
    const QStringList ms_labels = {tr("1:1"), tr("1:2"), tr("Auto")};
    for (int i = 0; i < ms_labels.size(); ++i) {
      auto* rb = new QRadioButton(ms_labels[i], ms_box);
      cpu_ms_group_->addButton(rb, i);
      ms_layout->addWidget(rb);
    }
    ms_layout->addStretch();
    hw_row->addWidget(ms_box, 1);

    eram_box_ = new QGroupBox(tr("Extended RAM (&E)"), page);
    eram_box_->setProperty("hw_equal_row", true);
    auto* eram_outer = new QVBoxLayout(eram_box_);
    CompactGroupBoxLayout(eram_outer);
    eram_banks_ = new QSpinBox(eram_box_);
    eram_banks_->setRange(0, 256);
    eram_outer->addStretch();
    auto* eram_row = new QHBoxLayout();
    eram_row->addStretch(1);
    eram_row->addWidget(eram_banks_);
    eram_row->addStretch(1);
    eram_row->addWidget(new QLabel(tr("x 32 KB"), eram_box_));
    eram_row->addStretch(1);
    eram_outer->addLayout(eram_row);
    eram_outer->addStretch();
    hw_row->addWidget(eram_box_, 1);
    v->addLayout(hw_row);

    auto* av_row = new QHBoxLayout();
    av_row->setSpacing(4);

    auto* display_box = new QGroupBox(tr("Display hardware"), page);
    display_box->setProperty("hw_equal_row", true);
    auto* display_layout = new QVBoxLayout(display_box);
    display_layout->setSpacing(2);
    hw_pcg_ = new QCheckBox(tr("Enable PCG (&P)"), display_box);
    hw_fv15k_ = new QCheckBox(tr("15KHz monitor mode (&1)"), display_box);
    hw_digitalpal_ = new QCheckBox(tr("Digital palette mode (&D)"), display_box);
    display_layout->addWidget(hw_pcg_);
    display_layout->addWidget(hw_fv15k_);
    display_layout->addWidget(hw_digitalpal_);
    display_layout->addStretch();
    av_row->addWidget(display_box, 1);

    auto* sound_board_box = new QGroupBox(tr("Sound board"), page);
    sound_board_box->setProperty("hw_equal_row", true);
    auto* sound_board_outer = new QVBoxLayout(sound_board_box);
    CompactGroupBoxLayout(sound_board_outer);
    sound_fm44_ = new QComboBox(sound_board_box);
    sound_fma8_ = new QComboBox(sound_board_box);
    PopulateFm44Combo(sound_fm44_);
    PopulateFmA8Combo(sound_fma8_);
    sound_board_outer->addStretch();
    auto* fm44_row = new QHBoxLayout();
    fm44_row->setSpacing(4);
    fm44_row->addWidget(new QLabel(tr("FM ($44h):"), sound_board_box));
    fm44_row->addWidget(sound_fm44_, 1);
    sound_board_outer->addLayout(fm44_row);
    auto* fma8_row = new QHBoxLayout();
    fma8_row->setSpacing(4);
    fma8_row->addWidget(new QLabel(tr("FM ($A8h):"), sound_board_box));
    fma8_row->addWidget(sound_fma8_, 1);
    sound_board_outer->addLayout(fma8_row);
    sound_board_outer->addStretch();
    av_row->addWidget(sound_board_box, 1);
    v->addLayout(av_row);

    connect(cpu_speed_, &QSlider::valueChanged, this, [this](int v) {
      cpu_speed_label_->setText(tr("%1%").arg(v * 10));
    });
    connect(cpu_nowait_, &QCheckBox::toggled, this, [this](bool on) {
      if (on) {
        cpu_burst_->setChecked(false);
      }
      updateCpuTab();
    });
    connect(cpu_burst_, &QCheckBox::toggled, this, [this](bool on) {
      if (on) {
        cpu_nowait_->setChecked(false);
      }
      updateCpuTab();
    });
    connect(cpu_nowait_, &QCheckBox::toggled, this, &ConfigDialog::updateCpuTab);
    connect(cpu_burst_, &QCheckBox::toggled, this, &ConfigDialog::updateCpuTab);

    v->addStretch(1);
    auto* hw_snapshot_note = new QLabel(
        tr("Settings on this tab are saved in snapshot files and restored when a "
           "snapshot is loaded, replacing the values shown here."),
        page);
    hw_snapshot_note->setWordWrap(true);
    v->addWidget(hw_snapshot_note);

    hardware_page_ = page;
    tabs_->addTab(page, tr("Hardware"));
  }

  // --- Audio/Video (host display + sound output) ---
  {
    auto* page = new QWidget(tabs_);
    auto* v = new QVBoxLayout(page);
    CompactVBox(v);

    auto* refresh_box = new QGroupBox(tr("Screen refresh ratio (&R)"), page);
    auto* refresh_layout = new QHBoxLayout(refresh_box);
    refresh_group_ = new QButtonGroup(refresh_box);
    const QStringList refresh_labels = {tr("1:1 (60Hz)"), tr("1:2 (30Hz)"),
                                        tr("1:3 (20Hz)"), tr("1:4 (15Hz)")};
    for (int i = 0; i < refresh_labels.size(); ++i) {
      auto* rb = new QRadioButton(refresh_labels[i], refresh_box);
      refresh_group_->addButton(rb, i + 1);
      refresh_layout->addWidget(rb);
    }
    v->addWidget(refresh_box);

    screen_force480_ = new QCheckBox(tr("Force 640x480 in fullscreen (&V)"), page);
    screen_lowpriority_ = new QCheckBox(tr("Lower draw priority (&L)"), page);
    screen_fullline_ = new QCheckBox(tr("Show even scanlines (&F)"), page);
    screen_vsync_ = new QCheckBox(tr("Sync to VSync in fullscreen (&S)"), page);
    auto* screen_grid = new QGridLayout();
    CompactGrid(screen_grid);
    screen_grid->addWidget(screen_force480_, 0, 0);
    screen_grid->addWidget(screen_lowpriority_, 0, 1);
    screen_grid->addWidget(screen_vsync_, 1, 0);
    screen_grid->addWidget(screen_fullline_, 1, 1);
    screen_grid->setColumnStretch(0, 1);
    screen_grid->setColumnStretch(1, 1);
    v->addLayout(screen_grid);

    connect(cpu_nowait_, &QCheckBox::toggled, this, &ConfigDialog::updateScreenTab);
    connect(cpu_burst_, &QCheckBox::toggled, this, &ConfigDialog::updateScreenTab);
    connect(cpu_speed_, &QSlider::valueChanged, this, &ConfigDialog::updateScreenTab);

    auto* rate_box = new QGroupBox(tr("Sample rate (&M)"), page);
    auto* rate_grid = new QGridLayout(rate_box);
    CompactGrid(rate_grid);
    sound_rate_group_ = new QButtonGroup(rate_box);
    struct RateItem {
      const char* label;
      int rate;
    };
    const RateItem rates[] = {
        {"22k", 22050}, {"44k", 44100}, {"48k", 48000}, {"55k", 55467},
        {"88k", 88200}, {"96k", 96000}, {"Silent", 0},
    };
    for (int i = 0; i < 7; ++i) {
      auto* rb = new QRadioButton(QString::fromLatin1(rates[i].label), rate_box);
      sound_rate_group_->addButton(rb, rates[i].rate);
      rate_grid->addWidget(rb, i / 4, i % 4);
    }
    v->addWidget(rate_box);

    auto* buf_row = new QHBoxLayout();
    buf_row->setSpacing(4);
    sound_buffer_ = new QSpinBox(page);
    sound_buffer_->setRange(50, 1000);
    sound_buffer_->setSingleStep(10);
    sound_buffer_->setSuffix(tr(" ms"));
    buf_row->addWidget(new QLabel(tr("Sound buffer (&B):"), page));
    buf_row->addWidget(sound_buffer_);
    buf_row->addStretch();
    v->addLayout(buf_row);

    sound_cmdsing_ = new QCheckBox(tr("Enable CMD SING (&G)"), page);
    sound_mixalways_ = new QCheckBox(tr("Prioritize sound mixing (&S)"), page);
    sound_precisemix_ = new QCheckBox(tr("High precision mixing (&P)"), page);
    sound_backend_ = new QComboBox(page);
    sound_backend_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    PopulateSoundBackendCombo(sound_backend_);
    sound_device_ = new QComboBox(page);
    sound_device_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    PopulateSoundDeviceCombo(sound_device_, "");
    sound_fmclock_ = new QCheckBox(tr("Use native FM clock for synthesis"), page);
    sound_lpf_ = new QCheckBox(tr("Enable LPF"), page);
    sound_lpffc_ = new QSpinBox(page);
    sound_lpffc_->setRange(3, 24);
    sound_lpffc_->setSuffix(tr(" kHz"));
    sound_lpforder_ = new QSpinBox(page);
    sound_lpforder_->setRange(2, 16);
    sound_lpforder_->setSingleStep(2);

    auto* opt_grid = new QGridLayout();
    CompactGrid(opt_grid);
    opt_grid->addWidget(sound_cmdsing_, 0, 0);
    opt_grid->addWidget(sound_mixalways_, 0, 1);
    opt_grid->addWidget(sound_precisemix_, 1, 0);
    opt_grid->addWidget(sound_fmclock_, 1, 1);

    auto* device_row = new QHBoxLayout();
    device_row->setContentsMargins(0, 0, 0, 0);
    device_row->setSpacing(4);
    device_row->addWidget(new QLabel(tr("Output device:"), page));
    device_row->addWidget(sound_device_, 1);
    device_row->addWidget(new QLabel(tr("Backend:"), page));
    device_row->addWidget(sound_backend_);
    opt_grid->addLayout(device_row, 2, 0, 1, 2);

    connect(sound_backend_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) {
              const QByteArray backend =
                  sound_backend_->currentData().toString().toUtf8();
              PopulateSoundDeviceCombo(sound_device_, backend.constData());
              markDirty();
            });

    auto* lpf_host = new QWidget(page);
    auto* lpf_row = new QHBoxLayout(lpf_host);
    lpf_row->setContentsMargins(0, 0, 0, 0);
    lpf_row->setSpacing(4);
    lpf_row->addWidget(sound_lpf_);
    lpf_row->addWidget(new QLabel(tr("Cutoff:"), lpf_host));
    lpf_row->addWidget(sound_lpffc_);
    lpf_row->addWidget(new QLabel(tr("Order:"), lpf_host));
    lpf_row->addWidget(sound_lpforder_);
    lpf_row->addStretch();
    opt_grid->addWidget(lpf_host, 3, 0, 1, 2);
    v->addLayout(opt_grid);

    connect(sound_lpf_, &QCheckBox::toggled, sound_lpffc_, &QSpinBox::setEnabled);
    connect(sound_lpf_, &QCheckBox::toggled, sound_lpforder_, &QSpinBox::setEnabled);

    FinishTabPage(v);
    av_page_ = page;
    tabs_->addTab(page, tr("Audio/Video"));
  }

  // --- Function ---
  {
    auto* page = new QWidget(tabs_);
    auto* v = new QVBoxLayout(page);
    CompactVBox(v);
    func_savedir_ = new QCheckBox(tr("Remember directory on exit (&D)"), page);
    func_savepos_ = new QCheckBox(tr("Remember window position (&W)"), page);
    if (M88QtIsWaylandSession()) {
      func_savepos_->setEnabled(false);
      func_savepos_->setChecked(false);
      func_savepos_->setToolTip(
          tr("Not available on Wayland (the compositor controls window placement)."));
    }
    func_askreset_ = new QCheckBox(tr("Confirm reset/exit (&E)"), page);
    func_suppressmenu_ = new QCheckBox(tr("Suppress menu via keyboard (&K)"), page);
    func_enablepad_ = new QCheckBox(tr("Use gamepad (&J)"), page);
    func_swappad_ = new QCheckBox(tr("Swap gamepad buttons (&S)"), page);
    func_resetf12_ = new QCheckBox(tr("F12 as Reset (&F)"), page);
    func_enablemouse_ = new QCheckBox(tr("Use serial mouse (&M)"), page);
    func_mousejoy_ = new QCheckBox(tr("Use bus mouse (&O)"), page);
    func_scrname_ = new QCheckBox(tr("Auto screenshot filename (&C)"), page);
    func_compsnap_ = new QCheckBox(tr("Compress snapshot files (&Z)"), page);
    func_idle_inhibit_ = new QCheckBox(tr("Inhibit display idle (&I)"), page);
    func_ime_kana_ = new QCheckBox(tr("Half-width kana via IME (&H)"), page);
    func_ime_kana_hint_ =
        new QLabel(tr("Half-width kana input for PC-8801: committed host IME text is sent as "
                      "half-width kana key strokes.\n"
                      "Requires a Japanese input engine on the desktop (Mozc, SKK, Anthy, etc.; "
                      "usually via fcitx).\n"
                      "When fcitx is running, IME on/off is synchronized via D-Bus from your "
                      "usual desktop IME controls."),
                   page);
    func_ime_kana_hint_->setWordWrap(true);

    auto* sense_row = new QHBoxLayout();
    func_mousesense_ = new QSlider(Qt::Horizontal, page);
    func_mousesense_->setRange(1, 10);
    func_mousesense_label_ = new QLabel(tr("Sensitivity (&P):"), page);
    func_mousesense_coarse_label_ = new QLabel(tr("coarse"), page);
    sense_row->addWidget(func_mousesense_label_);
    sense_row->addWidget(func_mousesense_, 1);
    sense_row->addWidget(func_mousesense_coarse_label_);

    auto* func_grid = new QGridLayout();
    CompactGrid(func_grid);
    for (QCheckBox* cb :
         {func_savedir_, func_savepos_, func_askreset_, func_suppressmenu_, func_enablepad_,
          func_swappad_, func_resetf12_, func_enablemouse_, func_mousejoy_, func_scrname_,
          func_compsnap_, func_idle_inhibit_}) {
      const int idx = func_grid->count();
      func_grid->addWidget(cb, idx / 2, idx % 2);
    }
    func_grid->setColumnStretch(0, 1);
    func_grid->setColumnStretch(1, 1);
    v->addLayout(func_grid);
    v->addLayout(sense_row);

    auto* ime_box = new QGroupBox(page);
    ShrinkGroupBox(ime_box);
    auto* ime_layout = new QVBoxLayout(ime_box);
    CompactGroupBoxLayout(ime_layout);
    ime_layout->addWidget(func_ime_kana_);
    ime_layout->addWidget(func_ime_kana_hint_);
    v->addWidget(ime_box);

    connect(func_enablepad_, &QCheckBox::toggled, this, &ConfigDialog::updateFunctionTab);
    connect(func_enablemouse_, &QCheckBox::toggled, this, &ConfigDialog::updateFunctionTab);
    connect(func_suppressmenu_, &QCheckBox::toggled, this, &ConfigDialog::updateFunctionTab);

    FinishTabPage(v);
    other_page_ = page;
    tabs_->addTab(page, tr("Other"));
  }

  // --- Volume ---
  {
    auto* page = new QWidget(tabs_);
    auto* outer = new QVBoxLayout(page);
    CompactVBox(outer);
    auto* form = new QFormLayout();
    CompactForm(form);
    auto setup_vol = [&](const QString& name, VolumeWidgets* w, int Config::*field) {
      w->field = &(config_.*field);
      w->slider = MakeVolumeSlider(0, page);
      w->label = new QLabel(page);
      w->label->setMinimumWidth(36);
      auto* row = new QHBoxLayout();
      row->addWidget(w->slider, 1);
      row->addWidget(w->label);
      form->addRow(name, row);
      connect(w->slider, &QSlider::valueChanged, this, [w](int val) {
        UpdateVolumeLabel(w->label, val);
      });
    };
    setup_vol(tr("&FM"), &vol_fm_, &Config::volfm);
    setup_vol(tr("&SSG"), &vol_ssg_, &Config::volssg);
    setup_vol(tr("AD&PCM"), &vol_adpcm_, &Config::voladpcm);
    setup_vol(tr("&Rhythm"), &vol_rhythm_, &Config::volrhythm);
    setup_vol(tr("&Bass Drum"), &vol_bd_, &Config::volbd);
    setup_vol(tr("Snare &Drum"), &vol_sd_, &Config::volsd);
    setup_vol(tr("Top &Cymbal"), &vol_top_, &Config::voltop);
    setup_vol(tr("&Hi-Hat"), &vol_hh_, &Config::volhh);
    setup_vol(tr("&Tom"), &vol_tom_, &Config::voltom);
    setup_vol(tr("R&im Shot"), &vol_rim_, &Config::volrim);

    auto* btn_row = new QHBoxLayout();
    auto* reset_src = new QPushButton(tr("Reset Source"), page);
    auto* reset_rhy = new QPushButton(tr("Reset Rhythm"), page);
    btn_row->addWidget(reset_src);
    btn_row->addWidget(reset_rhy);
    btn_row->addStretch();
    form->addRow(btn_row);

    connect(reset_src, &QPushButton::clicked, this, [this]() {
      vol_fm_.slider->setValue(0);
      vol_ssg_.slider->setValue(-3);
      vol_adpcm_.slider->setValue(0);
      vol_rhythm_.slider->setValue(0);
    });
    connect(reset_rhy, &QPushButton::clicked, this, [this]() {
      vol_bd_.slider->setValue(0);
      vol_sd_.slider->setValue(0);
      vol_top_.slider->setValue(0);
      vol_hh_.slider->setValue(0);
      vol_tom_.slider->setValue(0);
      vol_rim_.slider->setValue(0);
    });

    outer->addLayout(form);
    FinishTabPage(outer);
    volume_page_ = page;
    tabs_->addTab(page, tr("Volume"));
  }

  // --- DIP-SW ---
  {
    dip_page_ = new QWidget(tabs_);
    auto* page = dip_page_;
    auto* outer = new QVBoxLayout(page);
    CompactVBox(outer);
    auto* grid = new QGridLayout();
    CompactGrid(grid);
    static const struct {
      const char* name;
      const char* on;
      const char* off;
    } kDipRows[] = {
        {QT_TR_NOOP("Boot mode (&M)"), QT_TR_NOOP("Terminal"), QT_TR_NOOP("BASIC")},
        {QT_TR_NOOP("Chars per line (&W)"), QT_TR_NOOP("80"), QT_TR_NOOP("40")},
        {QT_TR_NOOP("Lines per screen (&H)"), QT_TR_NOOP("25"), QT_TR_NOOP("20")},
        {QT_TR_NOOP("S parameter (&S)"), QT_TR_NOOP("Enable"), QT_TR_NOOP("Disable")},
        {QT_TR_NOOP("DEL code (&L)"), QT_TR_NOOP("Process"), QT_TR_NOOP("Ignore")},
        {QT_TR_NOOP("Parity check (&P)"), QT_TR_NOOP("On"), QT_TR_NOOP("Off")},
        {QT_TR_NOOP("Parity type (&Q)"), QT_TR_NOOP("Even"), QT_TR_NOOP("Odd")},
        {QT_TR_NOOP("Data bits (&D)"), QT_TR_NOOP("8-bit"), QT_TR_NOOP("7-bit")},
        {QT_TR_NOOP("Stop bits (&T)"), QT_TR_NOOP("2-bit"), QT_TR_NOOP("1-bit")},
        {QT_TR_NOOP("X parameter (&X)"), QT_TR_NOOP("Enable"), QT_TR_NOOP("Disable")},
        {QT_TR_NOOP("Duplex (&U)"), QT_TR_NOOP("Half"), QT_TR_NOOP("Full")},
        {QT_TR_NOOP("Boot from FDD (&B)"), QT_TR_NOOP("Auto"), QT_TR_NOOP("No")},
    };
    for (int i = 0; i < 12; ++i) {
      auto* name = new QLabel(tr(kDipRows[i].name), page);
      grid->addWidget(name, i, 0);
      dip_groups_[i] = new QButtonGroup(page);
      auto* on = new QRadioButton(tr(kDipRows[i].on), page);
      auto* off = new QRadioButton(tr(kDipRows[i].off), page);
      dip_groups_[i]->addButton(on, 0);
      dip_groups_[i]->addButton(off, 1);
      grid->addWidget(off, i, 1);
      grid->addWidget(on, i, 2);
    }
    outer->addLayout(grid);
    FinishTabPage(outer);
    tabs_->addTab(page, tr("DIP-SW"));
  }

  // --- Env ---
  {
    auto* page = new QWidget(tabs_);
    auto* v = new QVBoxLayout(page);
    CompactVBox(v);
    auto* key_box = new QGroupBox(tr("Host keyboard"), page);
    auto* key_layout = new QVBoxLayout(key_box);
    key_layout->setSpacing(2);
    keytype_group_ = new QButtonGroup(key_box);
    auto* key106 = new QRadioButton(tr("Japanese 106-key (&J)"), key_box);
    auto* key101 = new QRadioButton(tr("101/104-key AT (&1)"), key_box);
    keytype_group_->addButton(key106, Config::AT106);
    keytype_group_->addButton(key101, Config::AT101);
    key_layout->addWidget(key106);
    key_layout->addWidget(key101);
    v->addWidget(key_box);
    connect(keytype_group_, &QButtonGroup::idClicked, this, &ConfigDialog::updateFunctionTab);
    FinishTabPage(v);
    env_page_ = page;
    tabs_->addTab(page, tr("Environment"));
  }

  for (QGroupBox* box : findChildren<QGroupBox*>()) {
    if (box->property("hw_equal_row").toBool()) {
      EqualRowGroupBox(box);
    } else {
      ShrinkGroupBox(box);
    }
  }
  for (int i = 0; i < tabs_->count(); ++i) {
    ShrinkTabPage(tabs_->widget(i));
  }

  auto* buttons = new QDialogButtonBox(
      QDialogButtonBox::Apply | QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  apply_button_ = buttons->button(QDialogButtonBox::Apply);
  apply_button_->setEnabled(false);
  connect(apply_button_, &QPushButton::clicked, this, &ConfigDialog::onApply);
  connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
    applyToConfig();
    accept();
  });
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  auto* button_row = new QHBoxLayout();
  auto* defaults_button = new QPushButton(tr("Standard settings (&D)"), this);
  connect(defaults_button, &QPushButton::clicked, this,
          &ConfigDialog::resetCurrentTabToDefaults);
  button_row->addWidget(defaults_button);
  button_row->addStretch();
  button_row->addWidget(buttons);
  layout->addLayout(button_row);

  connectDirtyTracking();
}

void ConfigDialog::applyResetRequiredTooltips() {
  // Screen: port40 15KHz bit is refreshed in Base::Reset only.
  SetResetRequiredTooltip(hw_fv15k_);

  // CPU: ERAM bank count reallocates extended RAM (Windows cfgpage PageChanged).
  SetResetRequiredTooltip(eram_banks_);
  SetResetRequiredTooltip(eram_box_);

  // DIP-SW: sw30/sw31/port40 are rebuilt from dipsw in Base::Reset only.
  if (dip_page_) {
    SetResetRequiredTooltip(dip_page_);
    for (QLabel* label : dip_page_->findChildren<QLabel*>()) {
      SetResetRequiredTooltip(label);
    }
    for (QAbstractButton* button : dip_page_->findChildren<QAbstractButton*>()) {
      SetResetRequiredTooltip(button);
    }
  }
  if (tabs_ && dip_page_) {
    const int dip_tab = tabs_->indexOf(dip_page_);
    if (dip_tab >= 0) {
      tabs_->setTabToolTip(dip_tab, tr(kResetTooltipText));
    }
  }
}

void ConfigDialog::installTooltipDelivery() {
  if (!tooltip_filter_) {
    tooltip_filter_ = new TooltipEventFilter(this);
  }

  const auto attach = [this](QWidget* widget) {
    if (!widget) {
      return;
    }
    widget->setAttribute(Qt::WA_AlwaysShowToolTips);
    widget->installEventFilter(tooltip_filter_);
    for (QObject* child : widget->children()) {
      if (auto* child_widget = qobject_cast<QWidget*>(child)) {
        child_widget->setAttribute(Qt::WA_AlwaysShowToolTips);
        child_widget->installEventFilter(tooltip_filter_);
      }
    }
  };

  setAttribute(Qt::WA_AlwaysShowToolTips);
  installEventFilter(tooltip_filter_);

  for (QWidget* widget : findChildren<QWidget*>()) {
    if (!widget->toolTip().isEmpty()) {
      attach(widget);
    }
  }

  if (tabs_) {
    attach(tabs_->tabBar());
  }
}

void ConfigDialog::connectDirtyTracking() {
  for (QSpinBox* spin : findChildren<QSpinBox*>()) {
    connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &ConfigDialog::markDirty);
  }
  for (QSlider* slider : findChildren<QSlider*>()) {
    connect(slider, &QSlider::valueChanged, this, &ConfigDialog::markDirty);
  }
  for (QCheckBox* box : findChildren<QCheckBox*>()) {
    connect(box, &QCheckBox::toggled, this, &ConfigDialog::markDirty);
  }
  for (QRadioButton* radio : findChildren<QRadioButton*>()) {
    connect(radio, &QRadioButton::toggled, this, &ConfigDialog::markDirty);
  }
  for (QComboBox* combo : findChildren<QComboBox*>()) {
    connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &ConfigDialog::markDirty);
  }
}

void ConfigDialog::setApplyEnabled(bool enabled) {
  if (apply_button_) {
    apply_button_->setEnabled(enabled);
  }
}

void ConfigDialog::markDirty() {
  setApplyEnabled(true);
}

void ConfigDialog::onApply() {
  applyToConfig();
  emit settingsApplied(config_);
  setApplyEnabled(false);
}

void ConfigDialog::loadHardwareTabFromConfig(const Config& cfg) {
  cpu_clock_mhz_->setValue(cfg.clock / 10);
  cpu_speed_->setValue(cfg.speed / 100);
  cpu_speed_label_->setText(tr("%1%").arg(cfg.speed / 10));
  cpu_nowait_->setChecked((cfg.flags & Config::fullspeed) != 0);
  cpu_burst_->setChecked((cfg.flags & Config::cpuburst) != 0);
  if (auto* btn = cpu_ms_group_->button(cfg.cpumode & 3)) {
    btn->setChecked(true);
  }
  cpu_no_subcpu_ctrl_->setChecked((cfg.flags & Config::subcpucontrol) == 0);
  cpu_enable_wait_->setChecked((cfg.flags & Config::enablewait) != 0);
  cpu_fdd_wait_->setChecked((cfg.flag2 & Config::fddnowait) == 0);
  eram_banks_->setValue(cfg.erambanks);
  hw_pcg_->setChecked((cfg.flags & Config::enablepcg) != 0);
  hw_fv15k_->setChecked((cfg.flags & Config::fv15k) != 0);
  hw_digitalpal_->setChecked((cfg.flags & Config::digitalpalette) != 0);
  SetComboByData(sound_fm44_, Fm44ModeFromConfig(cfg));
  SetComboByData(sound_fma8_, FmA8ModeFromConfig(cfg));
  updateCpuTab();
}

void ConfigDialog::loadScreenSectionFromConfig(const Config& cfg) {
  if (auto* btn = refresh_group_->button(cfg.refreshtiming)) {
    btn->setChecked(true);
  }
  screen_force480_->setChecked((cfg.flags & Config::force480) != 0);
  screen_lowpriority_->setChecked((cfg.flags & Config::drawprioritylow) != 0);
  screen_fullline_->setChecked((cfg.flags & Config::fullline) != 0);
  screen_vsync_->setChecked((cfg.flag2 & Config::synctovsync) != 0);
  updateScreenTab();
}

void ConfigDialog::loadOtherTabFromConfig(const Config& cfg, bool wayland_idle,
                                          bool ime_kana) {
  func_savedir_->setChecked((cfg.flags & Config::savedirectory) != 0);
  func_savepos_->setChecked(func_savepos_->isEnabled() &&
                           (cfg.flag2 & Config::saveposition) != 0);
  func_askreset_->setChecked((cfg.flags & Config::askbeforereset) != 0);
  func_suppressmenu_->setChecked((cfg.flags & Config::suppressmenu) != 0);
  func_enablepad_->setChecked((cfg.flags & Config::enablepad) != 0);
  func_swappad_->setChecked((cfg.flags & Config::swappadbuttons) != 0);
  func_resetf12_->setChecked((cfg.flags & Config::disablef12reset) == 0);
  func_enablemouse_->setChecked(M88MouseInputAvailable() &&
                                (cfg.flags & Config::enablemouse) != 0);
  func_mousejoy_->setChecked(M88MouseInputAvailable() &&
                             (cfg.flags & Config::mousejoymode) != 0);
  func_mousesense_->setValue(static_cast<int>(cfg.mousesensibility));
  func_scrname_->setChecked((cfg.flag2 & Config::genscrnshotname) != 0);
  func_compsnap_->setChecked((cfg.flag2 & Config::compresssnapshot) != 0);
  func_idle_inhibit_->setChecked(wayland_idle);
  func_ime_kana_->setChecked(ime_kana);
  updateFunctionTab();
}

void ConfigDialog::loadSoundSectionFromConfig(const Config& cfg) {
  if (auto* btn = sound_rate_group_->button(cfg.sound)) {
    btn->setChecked(true);
  } else if (cfg.sound == 0) {
    sound_rate_group_->button(0)->setChecked(true);
  }
  sound_buffer_->setValue(static_cast<int>(cfg.soundbuffer));
  sound_cmdsing_->setChecked((cfg.flags & Config::disablesing) == 0);
  sound_mixalways_->setChecked((cfg.flags & Config::mixsoundalways) != 0);
  sound_precisemix_->setChecked((cfg.flags & Config::precisemixing) != 0);
  sound_fmclock_->setChecked((cfg.flag2 & Config::usefmclock) != 0);
  {
    const QString saved_backend = QString::fromUtf8(cfg.audiobackend);
    int backend_idx = sound_backend_->findData(saved_backend);
    if (backend_idx < 0 && !saved_backend.isEmpty()) {
      sound_backend_->addItem(saved_backend, saved_backend);
      backend_idx = sound_backend_->count() - 1;
    }
    sound_backend_->setCurrentIndex(backend_idx >= 0 ? backend_idx : 0);

    const QByteArray backend = sound_backend_->currentData().toString().toUtf8();
    const QString saved_device = QString::fromUtf8(cfg.audiodevice);
    PopulateSoundDeviceCombo(sound_device_, backend.constData(), saved_device);
  }
  sound_lpf_->setChecked((cfg.flag2 & Config::lpfenable) != 0);
  sound_lpffc_->setValue(static_cast<int>(cfg.lpffc / 1000));
  sound_lpforder_->setValue(static_cast<int>(cfg.lpforder));
  sound_lpffc_->setEnabled(sound_lpf_->isChecked());
  sound_lpforder_->setEnabled(sound_lpf_->isChecked());
}

void ConfigDialog::loadAvTabFromConfig(const Config& cfg) {
  loadScreenSectionFromConfig(cfg);
  loadSoundSectionFromConfig(cfg);
}

void ConfigDialog::loadVolumeTabFromConfig(const Config& cfg) {
  auto load_vol = [&cfg](VolumeWidgets* w, int Config::*field) {
    const int val = cfg.*field;
    w->slider->setValue(val);
    UpdateVolumeLabel(w->label, val);
  };
  load_vol(&vol_fm_, &Config::volfm);
  load_vol(&vol_ssg_, &Config::volssg);
  load_vol(&vol_adpcm_, &Config::voladpcm);
  load_vol(&vol_rhythm_, &Config::volrhythm);
  load_vol(&vol_bd_, &Config::volbd);
  load_vol(&vol_sd_, &Config::volsd);
  load_vol(&vol_top_, &Config::voltop);
  load_vol(&vol_hh_, &Config::volhh);
  load_vol(&vol_tom_, &Config::voltom);
  load_vol(&vol_rim_, &Config::volrim);
}

void ConfigDialog::loadDipTabFromConfig(const Config& cfg) {
  for (int i = 0; i < 12; ++i) {
    const bool bit_on = (cfg.dipsw & (1 << i)) == 0;
    if (auto* btn = dip_groups_[i]->button(bit_on ? 0 : 1)) {
      btn->setChecked(true);
    }
  }
}

void ConfigDialog::loadEnvTabFromConfig(const Config& cfg) {
  const int host_key = cfg.keytype == Config::PC98 ? Config::AT106 : cfg.keytype;
  if (auto* btn = keytype_group_->button(host_key)) {
    btn->setChecked(true);
  }
}

void ConfigDialog::loadFromConfig() {
  loadHardwareTabFromConfig(config_);
  loadAvTabFromConfig(config_);
  loadOtherTabFromConfig(config_, M88WaylandIdleInhibitEnabled(), M88ImeHalfKanaEnabled());
  loadVolumeTabFromConfig(config_);
  loadDipTabFromConfig(config_);
  loadEnvTabFromConfig(config_);
}

void ConfigDialog::resetCurrentTabToDefaults() {
  if (!tabs_) {
    return;
  }
  const QWidget* page = tabs_->currentWidget();
  if (page == hardware_page_) {
    loadHardwareTabFromConfig(default_config_);
  } else if (page == av_page_) {
    loadAvTabFromConfig(default_config_);
  } else if (page == other_page_) {
    loadOtherTabFromConfig(default_config_, default_wayland_idle_, default_ime_kana_);
  } else if (page == volume_page_) {
    loadVolumeTabFromConfig(default_config_);
  } else if (page == dip_page_) {
    loadDipTabFromConfig(default_config_);
  } else if (page == env_page_) {
    loadEnvTabFromConfig(default_config_);
  }
  markDirty();
}

void ConfigDialog::applyToConfig() {
  config_.clock = LimitInt(cpu_clock_mhz_->value(), 100, 1) * 10;
  config_.speed = cpu_speed_->value() * 100;

  config_.flags &= ~(Config::fullspeed | Config::cpuburst | Config::subcpucontrol |
                     Config::enablewait);
  if (cpu_nowait_->isChecked()) {
    config_.flags |= Config::fullspeed;
  }
  if (cpu_burst_->isChecked()) {
    config_.flags |= Config::cpuburst;
  }
  if (!cpu_no_subcpu_ctrl_->isChecked()) {
    config_.flags |= Config::subcpucontrol;
  }
  if (cpu_enable_wait_->isChecked()) {
    config_.flags |= Config::enablewait;
  }
  config_.flag2 &= ~Config::fddnowait;
  if (!cpu_fdd_wait_->isChecked()) {
    config_.flag2 |= Config::fddnowait;
  }
  config_.cpumode =
      static_cast<Config::CPUType>(cpu_ms_group_->checkedId());
  config_.erambanks = eram_banks_->value();

  config_.flags &= ~(Config::enablepcg | Config::fv15k | Config::digitalpalette);
  if (hw_pcg_->isChecked()) config_.flags |= Config::enablepcg;
  if (hw_fv15k_->isChecked()) config_.flags |= Config::fv15k;
  if (hw_digitalpal_->isChecked()) config_.flags |= Config::digitalpalette;

  config_.flags &= ~(Config::enableopna | Config::opnona8 | Config::opnaona8);
  config_.flag2 &= ~Config::disableopn44;
  switch (sound_fm44_->currentData().toInt()) {
    case 1:
      config_.flags |= Config::enableopna;
      break;
    case 2:
      config_.flag2 |= Config::disableopn44;
      break;
    default:
      break;
  }
  switch (sound_fma8_->currentData().toInt()) {
    case 0:
      config_.flags |= Config::opnona8;
      break;
    case 1:
      config_.flags |= Config::opnaona8;
      break;
    default:
      break;
  }

  config_.refreshtiming = refresh_group_->checkedId();
  config_.flags &= ~(Config::force480 | Config::drawprioritylow | Config::fullline);
  if (screen_force480_->isChecked()) config_.flags |= Config::force480;
  if (screen_lowpriority_->isChecked()) config_.flags |= Config::drawprioritylow;
  if (screen_fullline_->isChecked()) config_.flags |= Config::fullline;
  config_.flag2 &= ~Config::synctovsync;
  if (screen_vsync_->isChecked()) config_.flag2 |= Config::synctovsync;

  config_.flags &= ~(Config::savedirectory | Config::askbeforereset | Config::suppressmenu |
                     Config::enablepad | Config::swappadbuttons |
                     Config::disablef12reset | Config::enablemouse | Config::mousejoymode);
  if (func_savedir_->isChecked()) config_.flags |= Config::savedirectory;
  if (func_askreset_->isChecked()) config_.flags |= Config::askbeforereset;
  if (func_suppressmenu_->isEnabled() && func_suppressmenu_->isChecked()) {
    config_.flags |= Config::suppressmenu;
  }
  if (func_enablepad_->isChecked()) config_.flags |= Config::enablepad;
  if (func_swappad_->isChecked()) config_.flags |= Config::swappadbuttons;
  if (!func_resetf12_->isChecked()) config_.flags |= Config::disablef12reset;
  if (M88MouseInputAvailable() && func_enablemouse_->isChecked()) {
    config_.flags |= Config::enablemouse;
  }
  if (M88MouseInputAvailable() && func_mousejoy_->isChecked()) {
    config_.flags |= Config::mousejoymode;
  }
  config_.flag2 &= ~(Config::saveposition | Config::genscrnshotname | Config::compresssnapshot);
  if (func_savepos_->isEnabled() && func_savepos_->isChecked()) {
    config_.flag2 |= Config::saveposition;
  }
  if (func_scrname_->isChecked()) config_.flag2 |= Config::genscrnshotname;
  if (func_compsnap_->isChecked()) config_.flag2 |= Config::compresssnapshot;
  config_.mousesensibility = static_cast<uint>(func_mousesense_->value());
  M88SetWaylandIdleInhibitEnabled(func_idle_inhibit_->isEnabled() &&
                                  func_idle_inhibit_->isChecked());
  M88SetImeHalfKanaEnabled(func_ime_kana_->isEnabled() && func_ime_kana_->isChecked());

  config_.sound = sound_rate_group_->checkedId();
  config_.soundbuffer =
      static_cast<uint>(LimitInt(sound_buffer_->value(), 1000, 50) / 10 * 10);
  config_.flags &= ~(Config::disablesing | Config::mixsoundalways | Config::precisemixing);
  if (!sound_cmdsing_->isChecked()) config_.flags |= Config::disablesing;
  if (sound_mixalways_->isChecked()) config_.flags |= Config::mixsoundalways;
  if (sound_precisemix_->isChecked()) config_.flags |= Config::precisemixing;
  config_.flag2 &= ~(Config::usefmclock | Config::lpfenable);
  if (sound_fmclock_->isChecked()) config_.flag2 |= Config::usefmclock;
  {
    const QString backend = sound_backend_->currentData().toString();
    const QByteArray backend_utf8 = backend.toUtf8();
    std::strncpy(config_.audiobackend, backend_utf8.constData(),
                 sizeof(config_.audiobackend) - 1);
    config_.audiobackend[sizeof(config_.audiobackend) - 1] = '\0';

    if (M88MiniaudioDevices::IsAutoBackend(config_.audiobackend)) {
      config_.audiodevice[0] = '\0';
    } else {
      const QString dev = sound_device_->currentData().toString();
      const QByteArray dev_utf8 = dev.toUtf8();
      std::strncpy(config_.audiodevice, dev_utf8.constData(),
                   sizeof(config_.audiodevice) - 1);
      config_.audiodevice[sizeof(config_.audiodevice) - 1] = '\0';
    }
  }
  if (sound_lpf_->isChecked()) config_.flag2 |= Config::lpfenable;
  config_.lpffc =
      static_cast<uint>(LimitInt(sound_lpffc_->value(), 24, 3) * 1000);
  config_.lpforder =
      static_cast<uint>(LimitInt(sound_lpforder_->value(), 16, 2) / 2 * 2);

  config_.volfm = vol_fm_.slider->value();
  config_.volssg = vol_ssg_.slider->value();
  config_.voladpcm = vol_adpcm_.slider->value();
  config_.volrhythm = vol_rhythm_.slider->value();
  config_.volbd = vol_bd_.slider->value();
  config_.volsd = vol_sd_.slider->value();
  config_.voltop = vol_top_.slider->value();
  config_.volhh = vol_hh_.slider->value();
  config_.voltom = vol_tom_.slider->value();
  config_.volrim = vol_rim_.slider->value();

  config_.dipsw = 0;
  for (int i = 0; i < 12; ++i) {
    if (dip_groups_[i]->checkedId() != 0) {
      config_.dipsw |= 1 << i;
    }
  }

  config_.keytype = static_cast<Config::KeyType>(keytype_group_->checkedId());
}

void ConfigDialog::updateCpuTab() {
  const bool full = cpu_nowait_->isChecked();
  const bool burst = cpu_burst_->isChecked();
  cpu_clock_mhz_->setEnabled(!full);
  cpu_speed_->setEnabled(!burst);
  cpu_speed_label_->setEnabled(!burst);
}

void ConfigDialog::updateScreenTab() {
  const bool disable_vsync = cpu_nowait_->isChecked() || cpu_burst_->isChecked() ||
                             cpu_speed_->value() != 10;
  screen_vsync_->setEnabled(!disable_vsync);
}

void ConfigDialog::updateFunctionTab() {
  const bool host_at101 =
      keytype_group_ && keytype_group_->checkedId() == static_cast<int>(Config::AT101);
  if (host_at101 && func_suppressmenu_->isChecked()) {
    func_suppressmenu_->blockSignals(true);
    func_suppressmenu_->setChecked(false);
    func_suppressmenu_->blockSignals(false);
  }
  func_suppressmenu_->setEnabled(!host_at101);
  if (host_at101) {
    func_suppressmenu_->setToolTip(
        tr("Not available with 101/104-key AT keyboard (Alt is not mapped to GRPH)."));
  } else {
    func_suppressmenu_->setToolTip(QString());
  }

  func_swappad_->setEnabled(func_enablepad_->isChecked());

  const bool mouse_ok = M88MouseInputAvailable();
  if (!mouse_ok) {
    if (func_enablemouse_->isChecked()) {
      func_enablemouse_->blockSignals(true);
      func_enablemouse_->setChecked(false);
      func_enablemouse_->blockSignals(false);
    }
    if (func_mousejoy_->isChecked()) {
      func_mousejoy_->blockSignals(true);
      func_mousejoy_->setChecked(false);
      func_mousejoy_->blockSignals(false);
    }
  }
  func_enablemouse_->setEnabled(mouse_ok);
  func_mousejoy_->setEnabled(mouse_ok && func_enablemouse_->isChecked());
  func_mousesense_->setEnabled(mouse_ok);
  func_mousesense_label_->setEnabled(mouse_ok);
  func_mousesense_coarse_label_->setEnabled(mouse_ok);
  if (!mouse_ok) {
    const QString mouse_tip =
        tr("Serial / bus mouse is not verified on the Linux Qt port.");
    func_enablemouse_->setToolTip(mouse_tip);
    func_mousejoy_->setToolTip(mouse_tip);
    func_mousesense_->setToolTip(mouse_tip);
  } else {
    func_enablemouse_->setToolTip(QString());
    func_mousejoy_->setToolTip(QString());
    func_mousesense_->setToolTip(QString());
  }

  if (mouse_ok) {
    if (func_suppressmenu_->isEnabled() && func_suppressmenu_->isChecked()) {
      func_enablemouse_->setChecked(false);
    }
    if (func_enablepad_->isChecked()) {
      func_enablemouse_->setChecked(false);
    }
    if (func_enablemouse_->isChecked()) {
      func_enablepad_->setChecked(false);
      if (func_suppressmenu_->isEnabled()) {
        func_suppressmenu_->setChecked(false);
      }
    }
  }

  const bool idle_ok = M88WaylandIdleInhibitAvailable();
  if (!idle_ok && func_idle_inhibit_->isChecked()) {
    func_idle_inhibit_->blockSignals(true);
    func_idle_inhibit_->setChecked(false);
    func_idle_inhibit_->blockSignals(false);
  }
  func_idle_inhibit_->setEnabled(idle_ok);
  if (!idle_ok) {
    func_idle_inhibit_->setToolTip(
        tr("Requires Wayland with idle-inhibit support (not available in this session)."));
  } else {
    func_idle_inhibit_->setToolTip(QString());
  }

  const bool ime_ok = LinuxIme::HostAvailable();
  if (!ime_ok && func_ime_kana_->isChecked()) {
    func_ime_kana_->blockSignals(true);
    func_ime_kana_->setChecked(false);
    func_ime_kana_->blockSignals(false);
  }
  func_ime_kana_->setEnabled(ime_ok);
  func_ime_kana_hint_->setEnabled(ime_ok);
  if (!ime_ok) {
    func_ime_kana_->setToolTip(
        tr("No host input method was detected at startup (fcitx, ibus, etc.)."));
  } else {
    func_ime_kana_->setToolTip(QString());
  }
}
