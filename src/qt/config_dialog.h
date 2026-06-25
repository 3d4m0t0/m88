#pragma once

#include "../pc88/config.h"

#include <QDialog>

class QTabWidget;

class QComboBox;

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
  void resetCurrentTabToDefaults();

private:
  void buildUi();
  void applyFixedDialogSize();
  void loadFromConfig();
  void loadHardwareTabFromConfig(const PC8801::Config& cfg);
  void loadAvTabFromConfig(const PC8801::Config& cfg);
  void loadOtherTabFromConfig(const PC8801::Config& cfg, bool wayland_idle, bool ime_kana);
  void loadScreenSectionFromConfig(const PC8801::Config& cfg);
  void loadSoundSectionFromConfig(const PC8801::Config& cfg);
  void loadVolumeTabFromConfig(const PC8801::Config& cfg);
  void loadDipTabFromConfig(const PC8801::Config& cfg);
  void loadEnvTabFromConfig(const PC8801::Config& cfg);
  void applyToConfig();
  void connectDirtyTracking();
  void applyResetRequiredTooltips();
  void installTooltipDelivery();
  void setApplyEnabled(bool enabled);
  void updateCpuTab();
  void updateScreenTab();
  void updateFunctionTab();

  PC8801::Config config_;
  PC8801::Config default_config_;
  bool default_wayland_idle_ = false;
  bool default_ime_kana_ = true;
  QTabWidget* tabs_ = nullptr;
  class QPushButton* apply_button_ = nullptr;
  QObject* tooltip_filter_ = nullptr;

  QWidget* hardware_page_ = nullptr;
  QWidget* av_page_ = nullptr;
  QWidget* other_page_ = nullptr;
  QWidget* volume_page_ = nullptr;

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
  class QCheckBox* hw_pcg_ = nullptr;
  class QCheckBox* hw_fv15k_ = nullptr;
  class QCheckBox* hw_digitalpal_ = nullptr;
  class QComboBox* sound_fm44_ = nullptr;
  class QComboBox* sound_fma8_ = nullptr;

  // Screen (host display)
  class QButtonGroup* refresh_group_ = nullptr;
  class QCheckBox* screen_force480_ = nullptr;
  class QCheckBox* screen_lowpriority_ = nullptr;
  class QCheckBox* screen_fullline_ = nullptr;
  class QCheckBox* screen_vsync_ = nullptr;

  // Function
  class QCheckBox* func_savedir_ = nullptr;
  class QCheckBox* func_savepos_ = nullptr;
  class QCheckBox* func_askreset_ = nullptr;
  class QCheckBox* func_suppressmenu_ = nullptr;
  class QCheckBox* func_enablepad_ = nullptr;
  class QCheckBox* func_swappad_ = nullptr;
  class QCheckBox* func_resetf12_ = nullptr;
  class QCheckBox* func_enablemouse_ = nullptr;
  class QCheckBox* func_mousejoy_ = nullptr;
  class QSlider* func_mousesense_ = nullptr;
  class QLabel* func_mousesense_label_ = nullptr;
  class QLabel* func_mousesense_coarse_label_ = nullptr;
  class QCheckBox* func_scrname_ = nullptr;
  class QCheckBox* func_compsnap_ = nullptr;
  class QCheckBox* func_idle_inhibit_ = nullptr;
  class QCheckBox* func_ime_kana_ = nullptr;
  class QLabel* func_ime_kana_hint_ = nullptr;

  // Sound
  class QButtonGroup* sound_rate_group_ = nullptr;
  class QSpinBox* sound_buffer_ = nullptr;
  class QCheckBox* sound_cmdsing_ = nullptr;
  class QCheckBox* sound_mixalways_ = nullptr;
  class QCheckBox* sound_precisemix_ = nullptr;
  class QComboBox* sound_device_ = nullptr;
  class QComboBox* sound_backend_ = nullptr;
  class QCheckBox* sound_fmclock_ = nullptr;
  class QCheckBox* sound_lpf_ = nullptr;
  class QSpinBox* sound_lpffc_ = nullptr;
  class QSpinBox* sound_lpforder_ = nullptr;

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
  QWidget* env_page_ = nullptr;
};
