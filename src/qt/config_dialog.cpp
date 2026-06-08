#include "config_dialog.h"

#include "../common/misc.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
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

namespace {

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
    label->setText(QObject::tr("Mute"));
  }
}

constexpr auto kResetTooltipText =
    "Requires a machine reset to\n"
    "take effect. (Control - Reset).";

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
  widget->setToolTip(QObject::tr(kResetTooltipText));
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

  // --- CPU ---
  {
    auto* page = new QWidget(tabs_);
    auto* v = new QVBoxLayout(page);
    CompactVBox(v);

    auto* speed_box = new QGroupBox(tr("Speed"), page);
    auto* speed_form = new QFormLayout(speed_box);
    CompactForm(speed_form);
    cpu_clock_mhz_ = new QSpinBox(speed_box);
    cpu_clock_mhz_->setRange(1, 100);
    cpu_clock_mhz_->setSuffix(tr(" MHz"));
    speed_form->addRow(tr("CPU clock (&C):"), cpu_clock_mhz_);

    auto* speed_row = new QHBoxLayout();
    cpu_speed_ = new QSlider(Qt::Horizontal, speed_box);
    cpu_speed_->setRange(2, 20);
    cpu_speed_label_ = new QLabel(speed_box);
    speed_row->addWidget(new QLabel(tr("Ratio (&B):"), speed_box));
    speed_row->addWidget(cpu_speed_, 1);
    speed_row->addWidget(cpu_speed_label_);
    speed_form->addRow(speed_row);

    cpu_nowait_ = new QCheckBox(tr("Full speed (&M)"), speed_box);
    cpu_burst_ = new QCheckBox(tr("Skip speed limit (&N)"), speed_box);
    speed_form->addRow(cpu_nowait_);
    speed_form->addRow(cpu_burst_);
    v->addWidget(speed_box);

    auto* ms_box = new QGroupBox(tr("Main/sub CPU ratio (&R)"), page);
    auto* ms_layout = new QHBoxLayout(ms_box);
    cpu_ms_group_ = new QButtonGroup(ms_box);
    for (const auto& item : {std::pair{tr("1:1"), Config::ms11},
                             std::pair{tr("2:1"), Config::ms21},
                             std::pair{tr("Auto"), Config::msauto}}) {
      auto* rb = new QRadioButton(item.first, ms_box);
      cpu_ms_group_->addButton(rb, static_cast<int>(item.second));
      ms_layout->addWidget(rb);
    }
    v->addWidget(ms_box);

    auto* misc_box = new QGroupBox(tr("Misc"), page);
    auto* misc_layout = new QVBoxLayout(misc_box);
    misc_layout->setSpacing(2);
    cpu_no_subcpu_ctrl_ = new QCheckBox(tr("Run sub CPU always (&S)"), misc_box);
    cpu_enable_wait_ = new QCheckBox(tr("Wait (&W)"), misc_box);
    cpu_fdd_wait_ = new QCheckBox(tr("FDD wait (&F)"), misc_box);
    misc_layout->addWidget(cpu_no_subcpu_ctrl_);
    misc_layout->addWidget(cpu_enable_wait_);
    misc_layout->addWidget(cpu_fdd_wait_);
    v->addWidget(misc_box);

    eram_box_ = new QGroupBox(tr("Extended RAM (&E)"), page);
    auto* eram_box = eram_box_;
    auto* eram_row = new QHBoxLayout(eram_box);
    eram_banks_ = new QSpinBox(eram_box);
    eram_banks_->setRange(0, 256);
    eram_row->addWidget(eram_banks_);
    eram_row->addWidget(new QLabel(tr("x 32 KB"), eram_box));
    eram_row->addStretch();
    v->addWidget(eram_box);

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

    FinishTabPage(v);
    tabs_->addTab(page, tr("CPU"));
  }

  // --- Screen ---
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

    screen_pcg_ = new QCheckBox(tr("Enable PCG (&P)"), page);
    screen_fv15k_ = new QCheckBox(tr("15KHz monitor mode (&1)"), page);
    screen_digitalpal_ = new QCheckBox(tr("Digital palette mode (&D)"), page);
    screen_force480_ = new QCheckBox(tr("Force 640x480 in fullscreen (&V)"), page);
    screen_lowpriority_ = new QCheckBox(tr("Lower draw priority (&L)"), page);
    screen_fullline_ = new QCheckBox(tr("Show even scanlines (&F)"), page);
    screen_vsync_ = new QCheckBox(tr("Sync to VSync in fullscreen (&S)"), page);
    v->addWidget(screen_pcg_);
    v->addWidget(screen_fv15k_);
    v->addWidget(screen_digitalpal_);
    v->addWidget(screen_force480_);
    v->addWidget(screen_lowpriority_);
    v->addWidget(screen_fullline_);
    v->addWidget(screen_vsync_);

    connect(cpu_nowait_, &QCheckBox::toggled, this, &ConfigDialog::updateScreenTab);
    connect(cpu_burst_, &QCheckBox::toggled, this, &ConfigDialog::updateScreenTab);
    connect(cpu_speed_, &QSlider::valueChanged, this, &ConfigDialog::updateScreenTab);

    FinishTabPage(v);
    tabs_->addTab(page, tr("Screen"));
  }

  // --- Function ---
  {
    auto* page = new QWidget(tabs_);
    auto* v = new QVBoxLayout(page);
    CompactVBox(v);
    func_savedir_ = new QCheckBox(tr("Remember directory on exit (&D)"), page);
    func_savepos_ = new QCheckBox(tr("Remember window position (&W)"), page);
    func_askreset_ = new QCheckBox(tr("Confirm reset/exit (&E)"), page);
    func_suppressmenu_ = new QCheckBox(tr("Suppress menu via keyboard (&K)"), page);
    func_arrowten_ = new QCheckBox(tr("Map arrow keys to ten-key (&H)"), page);
    func_enablepad_ = new QCheckBox(tr("Use gamepad (&J)"), page);
    func_swappad_ = new QCheckBox(tr("Swap gamepad buttons (&S)"), page);
    func_resetf12_ = new QCheckBox(tr("F12 as Reset (&F)"), page);
    func_enablemouse_ = new QCheckBox(tr("Use serial mouse (&M)"), page);
    func_mousejoy_ = new QCheckBox(tr("Use bus mouse (&O)"), page);
    func_scrname_ = new QCheckBox(tr("Auto screenshot filename (&C)"), page);
    func_compsnap_ = new QCheckBox(tr("Compress snapshot files (&Z)"), page);

    auto* sense_row = new QHBoxLayout();
    func_mousesense_ = new QSlider(Qt::Horizontal, page);
    func_mousesense_->setRange(1, 10);
    sense_row->addWidget(new QLabel(tr("Sensitivity (&P):"), page));
    sense_row->addWidget(func_mousesense_, 1);
    sense_row->addWidget(new QLabel(tr("coarse"), page));

    for (QCheckBox* cb :
         {func_savedir_, func_savepos_, func_askreset_, func_suppressmenu_, func_arrowten_,
          func_enablepad_, func_swappad_, func_resetf12_, func_enablemouse_, func_mousejoy_,
          func_scrname_, func_compsnap_}) {
      v->addWidget(cb);
    }
    v->addLayout(sense_row);

    connect(func_enablepad_, &QCheckBox::toggled, this, &ConfigDialog::updateFunctionTab);
    connect(func_enablemouse_, &QCheckBox::toggled, this, &ConfigDialog::updateFunctionTab);
    connect(func_suppressmenu_, &QCheckBox::toggled, this, &ConfigDialog::updateFunctionTab);

    FinishTabPage(v);
    tabs_->addTab(page, tr("Other"));
  }

  // --- Sound ---
  {
    auto* page = new QWidget(tabs_);
    auto* v = new QVBoxLayout(page);
    CompactVBox(v);

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

    auto* fm_row = new QHBoxLayout();
    fm_row->setSpacing(4);

    auto* fm44_box = new QGroupBox(tr("FM ($44h)"), page);
    auto* fm44_layout = new QVBoxLayout(fm44_box);
    CompactGroupBoxLayout(fm44_layout);
    sound44_group_ = new QButtonGroup(fm44_box);
    auto* opn = new QRadioButton(tr("OPN (&1)"), fm44_box);
    auto* opna = new QRadioButton(tr("OPNA (&2)"), fm44_box);
    auto* none44 = new QRadioButton(tr("None"), fm44_box);
    sound44_group_->addButton(opn, 0);
    sound44_group_->addButton(opna, 1);
    sound44_group_->addButton(none44, 2);
    fm44_layout->addWidget(opn);
    fm44_layout->addWidget(opna);
    fm44_layout->addWidget(none44);
    fm_row->addWidget(fm44_box, 1);

    auto* fma8_box = new QGroupBox(tr("FM ($A8h)"), page);
    auto* fma8_layout = new QVBoxLayout(fma8_box);
    CompactGroupBoxLayout(fma8_layout);
    sounda8_group_ = new QButtonGroup(fma8_box);
    auto* a8opn = new QRadioButton(tr("OPN (MkII)"), fma8_box);
    auto* a8opna = new QRadioButton(tr("OPNA (MkIII)"), fma8_box);
    auto* a8none = new QRadioButton(tr("None"), fma8_box);
    sounda8_group_->addButton(a8opn, 0);
    sounda8_group_->addButton(a8opna, 1);
    sounda8_group_->addButton(a8none, 2);
    fma8_layout->addWidget(a8opn);
    fma8_layout->addWidget(a8opna);
    fma8_layout->addWidget(a8none);
    fm_row->addWidget(fma8_box, 1);
    v->addLayout(fm_row);

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
    sound_waveout_ = new QCheckBox(tr("Play via WaveOut API (&W)"), page);
    sound_waveout_->setEnabled(false);
    sound_dsnotify_ = new QCheckBox(tr("Use DirectSound notify"), page);
    sound_dsnotify_->setEnabled(false);
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
    opt_grid->addWidget(sound_waveout_, 2, 0);
    opt_grid->addWidget(sound_dsnotify_, 2, 1);

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
    tabs_->addTab(page, tr("Sound"));
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
    auto* defaults = new QPushButton(tr("Default"), page);
    connect(defaults, &QPushButton::clicked, this, [this]() {
      config_.dipsw = 1829;
      loadFromConfig();
    });
    grid->addWidget(defaults, 12, 0, 1, 2);
    outer->addLayout(grid);
    FinishTabPage(outer);
    tabs_->addTab(page, tr("DIP-SW"));
  }

  // --- Env ---
  {
    auto* page = new QWidget(tabs_);
    auto* v = new QVBoxLayout(page);
    CompactVBox(v);
    auto* key_box = new QGroupBox(tr("Keyboard"), page);
    auto* key_layout = new QVBoxLayout(key_box);
    key_layout->setSpacing(2);
    keytype_group_ = new QButtonGroup(key_box);
    auto* key106 = new QRadioButton(tr("Japanese 106-key (&J)"), key_box);
    auto* key98 = new QRadioButton(tr("PC-9801 keyboard (&9)"), key_box);
    auto* key101 = new QRadioButton(tr("101/104-key AT (&1)"), key_box);
    keytype_group_->addButton(key106, Config::AT106);
    keytype_group_->addButton(key98, Config::PC98);
    keytype_group_->addButton(key101, Config::AT101);
    key_layout->addWidget(key106);
    key_layout->addWidget(key98);
    key_layout->addWidget(key101);
    v->addWidget(key_box);
    env_placesbar_ = new QCheckBox(tr("Show places bar in file dialogs (&P)"), page);
    env_placesbar_->setEnabled(false);
    v->addWidget(env_placesbar_);
    FinishTabPage(v);
    tabs_->addTab(page, tr("Environment"));
  }

  // --- ROMEO ---
  {
    auto* page = new QWidget(tabs_);
    auto* outer = new QVBoxLayout(page);
    CompactVBox(outer);
    auto* form = new QFormLayout();
    CompactForm(form);
    auto* row = new QHBoxLayout();
    romeo_latency_ = new QSlider(Qt::Horizontal, page);
    romeo_latency_->setRange(0, 500);
    romeo_latency_label_ = new QLabel(page);
    row->addWidget(romeo_latency_, 1);
    row->addWidget(romeo_latency_label_);
    form->addRow(tr("Latency:"), row);
    connect(romeo_latency_, &QSlider::valueChanged, this, [this](int ms) {
      romeo_latency_label_->setText(tr("%1 ms").arg(ms));
    });
    outer->addLayout(form);
    FinishTabPage(outer);
    tabs_->addTab(page, tr("ROMEO"));
  }

  for (QGroupBox* box : findChildren<QGroupBox*>()) {
    ShrinkGroupBox(box);
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
  layout->addWidget(buttons);

  connectDirtyTracking();
}

void ConfigDialog::applyResetRequiredTooltips() {
  // Screen: port40 15KHz bit is refreshed in Base::Reset only.
  SetResetRequiredTooltip(screen_fv15k_);

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

void ConfigDialog::loadFromConfig() {
  cpu_clock_mhz_->setValue(config_.clock / 10);
  cpu_speed_->setValue(config_.speed / 100);
  cpu_speed_label_->setText(tr("%1%").arg(config_.speed / 10));
  cpu_nowait_->setChecked((config_.flags & Config::fullspeed) != 0);
  cpu_burst_->setChecked((config_.flags & Config::cpuburst) != 0);
  if (auto* btn = cpu_ms_group_->button(config_.cpumode & 3)) {
    btn->setChecked(true);
  }
  cpu_no_subcpu_ctrl_->setChecked((config_.flags & Config::subcpucontrol) == 0);
  cpu_enable_wait_->setChecked((config_.flags & Config::enablewait) != 0);
  cpu_fdd_wait_->setChecked((config_.flag2 & Config::fddnowait) == 0);
  eram_banks_->setValue(config_.erambanks);

  if (auto* btn = refresh_group_->button(config_.refreshtiming)) {
    btn->setChecked(true);
  }
  screen_pcg_->setChecked((config_.flags & Config::enablepcg) != 0);
  screen_fv15k_->setChecked((config_.flags & Config::fv15k) != 0);
  screen_digitalpal_->setChecked((config_.flags & Config::digitalpalette) != 0);
  screen_force480_->setChecked((config_.flags & Config::force480) != 0);
  screen_lowpriority_->setChecked((config_.flags & Config::drawprioritylow) != 0);
  screen_fullline_->setChecked((config_.flags & Config::fullline) != 0);
  screen_vsync_->setChecked((config_.flag2 & Config::synctovsync) != 0);

  func_savedir_->setChecked((config_.flags & Config::savedirectory) != 0);
  func_savepos_->setChecked((config_.flag2 & Config::saveposition) != 0);
  func_askreset_->setChecked((config_.flags & Config::askbeforereset) != 0);
  func_suppressmenu_->setChecked((config_.flags & Config::suppressmenu) != 0);
  func_arrowten_->setChecked((config_.flags & Config::usearrowfor10) != 0);
  func_enablepad_->setChecked((config_.flags & Config::enablepad) != 0);
  func_swappad_->setChecked((config_.flags & Config::swappadbuttons) != 0);
  func_resetf12_->setChecked((config_.flags & Config::disablef12reset) == 0);
  func_enablemouse_->setChecked((config_.flags & Config::enablemouse) != 0);
  func_mousejoy_->setChecked((config_.flags & Config::mousejoymode) != 0);
  func_mousesense_->setValue(static_cast<int>(config_.mousesensibility));
  func_scrname_->setChecked((config_.flag2 & Config::genscrnshotname) != 0);
  func_compsnap_->setChecked((config_.flag2 & Config::compresssnapshot) != 0);

  if (auto* btn = sound_rate_group_->button(config_.sound)) {
    btn->setChecked(true);
  } else if (config_.sound == 0) {
    sound_rate_group_->button(0)->setChecked(true);
  }
  if (config_.flag2 & Config::disableopn44) {
    sound44_group_->button(2)->setChecked(true);
  } else if (config_.flags & Config::enableopna) {
    sound44_group_->button(1)->setChecked(true);
  } else {
    sound44_group_->button(0)->setChecked(true);
  }
  if (config_.flags & Config::opnaona8) {
    sounda8_group_->button(1)->setChecked(true);
  } else if (config_.flags & Config::opnona8) {
    sounda8_group_->button(0)->setChecked(true);
  } else {
    sounda8_group_->button(2)->setChecked(true);
  }
  sound_buffer_->setValue(static_cast<int>(config_.soundbuffer));
  sound_cmdsing_->setChecked((config_.flags & Config::disablesing) == 0);
  sound_mixalways_->setChecked((config_.flags & Config::mixsoundalways) != 0);
  sound_precisemix_->setChecked((config_.flags & Config::precisemixing) != 0);
  sound_fmclock_->setChecked((config_.flag2 & Config::usefmclock) != 0);
  sound_lpf_->setChecked((config_.flag2 & Config::lpfenable) != 0);
  sound_lpffc_->setValue(static_cast<int>(config_.lpffc / 1000));
  sound_lpforder_->setValue(static_cast<int>(config_.lpforder));
  sound_lpffc_->setEnabled(sound_lpf_->isChecked());
  sound_lpforder_->setEnabled(sound_lpf_->isChecked());

  auto load_vol = [](VolumeWidgets* w) {
    w->slider->setValue(*w->field);
    UpdateVolumeLabel(w->label, *w->field);
  };
  load_vol(&vol_fm_);
  load_vol(&vol_ssg_);
  load_vol(&vol_adpcm_);
  load_vol(&vol_rhythm_);
  load_vol(&vol_bd_);
  load_vol(&vol_sd_);
  load_vol(&vol_top_);
  load_vol(&vol_hh_);
  load_vol(&vol_tom_);
  load_vol(&vol_rim_);

  for (int i = 0; i < 12; ++i) {
    const bool bit_on = (config_.dipsw & (1 << i)) == 0;
    if (auto* btn = dip_groups_[i]->button(bit_on ? 0 : 1)) {
      btn->setChecked(true);
    }
  }

  if (auto* btn = keytype_group_->button(config_.keytype)) {
    btn->setChecked(true);
  }
  env_placesbar_->setChecked((config_.flag2 & Config::showplacesbar) != 0);

  romeo_latency_->setValue(config_.romeolatency);
  romeo_latency_label_->setText(tr("%1 ms").arg(config_.romeolatency));

  updateCpuTab();
  updateScreenTab();
  updateFunctionTab();
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
  config_.cpumode = static_cast<Config::CPUType>(cpu_ms_group_->checkedId());
  config_.erambanks = eram_banks_->value();

  config_.refreshtiming = refresh_group_->checkedId();
  config_.flags &= ~(Config::enablepcg | Config::fv15k | Config::digitalpalette |
                     Config::force480 | Config::drawprioritylow | Config::fullline);
  if (screen_pcg_->isChecked()) config_.flags |= Config::enablepcg;
  if (screen_fv15k_->isChecked()) config_.flags |= Config::fv15k;
  if (screen_digitalpal_->isChecked()) config_.flags |= Config::digitalpalette;
  if (screen_force480_->isChecked()) config_.flags |= Config::force480;
  if (screen_lowpriority_->isChecked()) config_.flags |= Config::drawprioritylow;
  if (screen_fullline_->isChecked()) config_.flags |= Config::fullline;
  config_.flag2 &= ~Config::synctovsync;
  if (screen_vsync_->isChecked()) config_.flag2 |= Config::synctovsync;

  config_.flags &= ~(Config::savedirectory | Config::askbeforereset | Config::suppressmenu |
                     Config::usearrowfor10 | Config::enablepad | Config::swappadbuttons |
                     Config::disablef12reset | Config::enablemouse | Config::mousejoymode);
  if (func_savedir_->isChecked()) config_.flags |= Config::savedirectory;
  if (func_askreset_->isChecked()) config_.flags |= Config::askbeforereset;
  if (func_suppressmenu_->isChecked()) config_.flags |= Config::suppressmenu;
  if (func_arrowten_->isChecked()) config_.flags |= Config::usearrowfor10;
  if (func_enablepad_->isChecked()) config_.flags |= Config::enablepad;
  if (func_swappad_->isChecked()) config_.flags |= Config::swappadbuttons;
  if (!func_resetf12_->isChecked()) config_.flags |= Config::disablef12reset;
  if (func_enablemouse_->isChecked()) config_.flags |= Config::enablemouse;
  if (func_mousejoy_->isChecked()) config_.flags |= Config::mousejoymode;
  config_.flag2 &= ~(Config::saveposition | Config::genscrnshotname | Config::compresssnapshot);
  if (func_savepos_->isChecked()) config_.flag2 |= Config::saveposition;
  if (func_scrname_->isChecked()) config_.flag2 |= Config::genscrnshotname;
  if (func_compsnap_->isChecked()) config_.flag2 |= Config::compresssnapshot;
  config_.mousesensibility = static_cast<uint>(func_mousesense_->value());

  config_.sound = sound_rate_group_->checkedId();
  config_.flags &= ~(Config::enableopna | Config::opnona8 | Config::opnaona8);
  config_.flag2 &= ~Config::disableopn44;
  switch (sound44_group_->checkedId()) {
    case 1:
      config_.flags |= Config::enableopna;
      break;
    case 2:
      config_.flag2 |= Config::disableopn44;
      break;
    default:
      break;
  }
  switch (sounda8_group_->checkedId()) {
    case 0:
      config_.flags |= Config::opnona8;
      break;
    case 1:
      config_.flags |= Config::opnaona8;
      break;
    default:
      break;
  }
  config_.soundbuffer =
      static_cast<uint>(LimitInt(sound_buffer_->value(), 1000, 50) / 10 * 10);
  config_.flags &= ~(Config::disablesing | Config::mixsoundalways | Config::precisemixing);
  if (!sound_cmdsing_->isChecked()) config_.flags |= Config::disablesing;
  if (sound_mixalways_->isChecked()) config_.flags |= Config::mixsoundalways;
  if (sound_precisemix_->isChecked()) config_.flags |= Config::precisemixing;
  config_.flag2 &= ~(Config::usefmclock | Config::lpfenable);
  if (sound_fmclock_->isChecked()) config_.flag2 |= Config::usefmclock;
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
  config_.flag2 &= ~Config::showplacesbar;
  if (env_placesbar_->isChecked()) config_.flag2 |= Config::showplacesbar;

  config_.romeolatency = romeo_latency_->value();
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
  func_swappad_->setEnabled(func_enablepad_->isChecked());
  func_mousejoy_->setEnabled(func_enablemouse_->isChecked());
  if (func_suppressmenu_->isChecked()) {
    func_enablemouse_->setChecked(false);
  }
  if (func_enablepad_->isChecked()) {
    func_enablemouse_->setChecked(false);
  }
  if (func_enablemouse_->isChecked()) {
    func_enablepad_->setChecked(false);
    func_suppressmenu_->setChecked(false);
  }
}
