#pragma once

#include "../pc88/config.h"

#include <QDialog>

class QTabWidget;

class ConfigDialog : public QDialog {
  Q_OBJECT

public:
  explicit ConfigDialog(PC8801::Config config, QWidget* parent = nullptr);

  PC8801::Config config() const { return config_; }

signals:
  void settingsApplied(PC8801::Config config);

private slots:
  void markDirty();
  void onApply();

private:
  void buildUi();
  void applyFixedDialogSize();
  void loadFromConfig();
  void applyToConfig();
  void connectDirtyTracking();
  void applyResetRequiredTooltips();
  void installTooltipDelivery();
  void setApplyEnabled(bool enabled);
  void updateCpuTab();
  void updateScreenTab();
  void updateFunctionTab();

  PC8801::Config config_;
  QTabWidget* tabs_ = nullptr;
  class QPushButton* apply_button_ = nullptr;
  QObject* tooltip_filter_ = nullptr;

  // CPU
  class QSpinBox* cpu_clock_mhz_ = nullptr;
  class QSlider* cpu_speed_ = nullptr;
  class QLabel* cpu_speed_label_ = nullptr;
  class QCheckBox* cpu_nowait_ = nullptr;
  class QCheckBox* cpu_burst_ = nullptr;
  class QButtonGroup* cpu_ms_group_ = nullptr;
  class QCheckBox* cpu_no_subcpu_ctrl_ = nullptr;
  class QCheckBox* cpu_enable_wait_ = nullptr;
  class QCheckBox* cpu_fdd_wait_ = nullptr;
  class QSpinBox* eram_banks_ = nullptr;

  // Screen
  class QButtonGroup* refresh_group_ = nullptr;
  class QCheckBox* screen_pcg_ = nullptr;
  class QCheckBox* screen_fv15k_ = nullptr;
  class QCheckBox* screen_digitalpal_ = nullptr;
  class QCheckBox* screen_force480_ = nullptr;
  class QCheckBox* screen_lowpriority_ = nullptr;
  class QCheckBox* screen_fullline_ = nullptr;
  class QCheckBox* screen_vsync_ = nullptr;

  // Function
  class QCheckBox* func_savedir_ = nullptr;
  class QCheckBox* func_savepos_ = nullptr;
  class QCheckBox* func_askreset_ = nullptr;
  class QCheckBox* func_suppressmenu_ = nullptr;
  class QCheckBox* func_arrowten_ = nullptr;
  class QCheckBox* func_enablepad_ = nullptr;
  class QCheckBox* func_swappad_ = nullptr;
  class QCheckBox* func_resetf12_ = nullptr;
  class QCheckBox* func_enablemouse_ = nullptr;
  class QCheckBox* func_mousejoy_ = nullptr;
  class QSlider* func_mousesense_ = nullptr;
  class QCheckBox* func_scrname_ = nullptr;
  class QCheckBox* func_compsnap_ = nullptr;

  // Sound
  class QButtonGroup* sound_rate_group_ = nullptr;
  class QButtonGroup* sound44_group_ = nullptr;
  class QButtonGroup* sounda8_group_ = nullptr;
  class QSpinBox* sound_buffer_ = nullptr;
  class QCheckBox* sound_cmdsing_ = nullptr;
  class QCheckBox* sound_mixalways_ = nullptr;
  class QCheckBox* sound_precisemix_ = nullptr;
  class QCheckBox* sound_waveout_ = nullptr;
  class QCheckBox* sound_fmclock_ = nullptr;
  class QCheckBox* sound_lpf_ = nullptr;
  class QSpinBox* sound_lpffc_ = nullptr;
  class QSpinBox* sound_lpforder_ = nullptr;
  class QCheckBox* sound_dsnotify_ = nullptr;

  // Volume sliders + labels
  struct VolumeWidgets {
    class QSlider* slider = nullptr;
    class QLabel* label = nullptr;
    int* field = nullptr;
  };
  VolumeWidgets vol_fm_;
  VolumeWidgets vol_ssg_;
  VolumeWidgets vol_adpcm_;
  VolumeWidgets vol_rhythm_;
  VolumeWidgets vol_bd_;
  VolumeWidgets vol_sd_;
  VolumeWidgets vol_top_;
  VolumeWidgets vol_hh_;
  VolumeWidgets vol_tom_;
  VolumeWidgets vol_rim_;

  // DIP-SW (12 bits, low=ON, high=OFF)
  class QWidget* dip_page_ = nullptr;
  class QGroupBox* eram_box_ = nullptr;
  class QButtonGroup* dip_groups_[12] = {};

  // Env
  class QButtonGroup* keytype_group_ = nullptr;
  class QCheckBox* env_placesbar_ = nullptr;

  // ROMEO
  class QSlider* romeo_latency_ = nullptr;
  class QLabel* romeo_latency_label_ = nullptr;
};
