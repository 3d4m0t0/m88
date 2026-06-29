#pragma once

#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLWidget>

#include <cstdint>
#include <vector>

class SharedRgbaFramebuffer;

class EmuGlView : public QOpenGLWidget, protected QOpenGLFunctions {
  Q_OBJECT

 public:
  explicit EmuGlView(QWidget* parent = nullptr);
  ~EmuGlView() override;

  void attachFramebuffer(SharedRgbaFramebuffer* framebuffer);
  void setScale(int scale);
  void setForce480Layout(bool enabled);
  void setImePreedit(const QString& text);
  bool glReady() const { return gl_ready_; }

 public slots:
  void refreshFrame();

 protected:
  void initializeGL() override;
  void paintGL() override;

 private:
  void uploadTexture(const unsigned char* rgba, unsigned int wid, unsigned int hei);
  void destroyTexture();
  void drawTexturedQuad(int x, int y, int dst_w, int dst_h, int vp_w, int vp_h);
  void drawImeOverlay();
  bool buildShaderProgram();

  SharedRgbaFramebuffer* framebuffer_ = nullptr;
  QOpenGLShaderProgram program_;
  QOpenGLBuffer vbo_;
  std::vector<unsigned char> staging_rgba_;
  GLuint tex_id_ = 0;
  unsigned int tex_w_ = 0;
  unsigned int tex_h_ = 0;
  unsigned int pending_w_ = 0;
  unsigned int pending_h_ = 0;
  uint64_t pending_serial_ = 0;
  bool frame_dirty_ = false;
  bool gl_ready_ = false;
  int scale_ = 1;
  bool force480_layout_ = false;
  uint64_t last_serial_ = 0;
  int emu_wid_ = 640;
  int emu_hei_ = 400;
  QString ime_preedit_;
};
