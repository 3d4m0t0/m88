#include "emu_gl_view.h"

#include "shared_rgba_framebuffer.h"

#include <QOpenGLShader>
#include <QPainter>
#include <QVector2D>
#include <QVector4D>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

constexpr char VERTEX_SHADER_GL[] =
    R"(#version 120
attribute vec2 aVert;
attribute vec2 aTex;
varying vec2 vTex;
uniform vec4 uRect;
uniform vec2 uWin;
void main()
{
  vec2 p=uRect.xy+aVert*uRect.zw;
  vec2 ndc=vec2(p.x/uWin.x*2.0-1.0,1.0-p.y/uWin.y*2.0);
  gl_Position=vec4(ndc,0.0,1.0);
  vTex=aTex;
}
)";

constexpr char FRAGMENT_SHADER_GL[] =
    R"(#version 120
varying vec2 vTex;
uniform sampler2D uTex;
void main()
{
  vec4 c=texture2D(uTex,vTex);
  gl_FragColor=vec4(c.rgb,1.0);
}
)";

constexpr char VERTEX_SHADER_ES[] =
    R"(attribute vec2 aVert;
attribute vec2 aTex;
varying vec2 vTex;
uniform vec4 uRect;
uniform vec2 uWin;
void main()
{
  vec2 p=uRect.xy+aVert*uRect.zw;
  vec2 ndc=vec2(p.x/uWin.x*2.0-1.0,1.0-p.y/uWin.y*2.0);
  gl_Position=vec4(ndc,0.0,1.0);
  vTex=aTex;
}
)";

constexpr char FRAGMENT_SHADER_ES[] =
    R"(precision mediump float;
varying vec2 vTex;
uniform sampler2D uTex;
void main()
{
  vec4 c=texture2D(uTex,vTex);
  gl_FragColor=vec4(c.rgb,1.0);
}
)";

constexpr float QUAD_VERTS[] = {
    0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f,
    0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f,
};

constexpr int kImeInlineBarHeight = 28;

}  // namespace

EmuGlView::EmuGlView(QWidget* parent) : QOpenGLWidget(parent) {
  setAttribute(Qt::WA_OpaquePaintEvent);
  setAutoFillBackground(false);
  setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);
}

EmuGlView::~EmuGlView() {
  if (isValid()) {
    makeCurrent();
    destroyTexture();
    doneCurrent();
  }
}

void EmuGlView::attachFramebuffer(SharedRgbaFramebuffer* framebuffer) {
  framebuffer_ = framebuffer;
}

void EmuGlView::setScale(int scale) {
  scale_ = std::max(1, scale);
  update();
}

void EmuGlView::setForce480Layout(bool enabled) {
  if (force480_layout_ == enabled) {
    return;
  }
  force480_layout_ = enabled;
  update();
}

void EmuGlView::setImePreedit(const QString& text) {
  if (ime_preedit_ == text) {
    return;
  }
  ime_preedit_ = text;
  update();
}

bool EmuGlView::buildShaderProgram() {
  const bool gles = context()->isOpenGLES();
  const char* const vs = gles ? VERTEX_SHADER_ES : VERTEX_SHADER_GL;
  const char* const fs = gles ? FRAGMENT_SHADER_ES : FRAGMENT_SHADER_GL;

  program_.removeAllShaders();
  if (!program_.addShaderFromSourceCode(QOpenGLShader::Vertex, vs)) {
    std::fprintf(stderr, "M88 GL vertex shader: %s\n",
                 program_.log().toUtf8().constData());
    return false;
  }
  if (!program_.addShaderFromSourceCode(QOpenGLShader::Fragment, fs)) {
    std::fprintf(stderr, "M88 GL fragment shader: %s\n",
                 program_.log().toUtf8().constData());
    return false;
  }
  program_.bindAttributeLocation("aVert", 0);
  program_.bindAttributeLocation("aTex", 1);
  if (!program_.link()) {
    std::fprintf(stderr, "M88 GL program link: %s\n",
                 program_.log().toUtf8().constData());
    return false;
  }
  return true;
}

void EmuGlView::initializeGL() {
  initializeOpenGLFunctions();
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glDisable(GL_BLEND);
  glDisable(GL_DEPTH_TEST);

  gl_ready_ = buildShaderProgram();
  if (!gl_ready_) {
    return;
  }

  vbo_.create();
  vbo_.bind();
  vbo_.allocate(QUAD_VERTS, static_cast<int>(sizeof(QUAD_VERTS)));
  vbo_.release();
}

void EmuGlView::destroyTexture() {
  if (tex_id_ != 0) {
    glDeleteTextures(1, &tex_id_);
    tex_id_ = 0;
  }
  tex_w_ = 0;
  tex_h_ = 0;
}

void EmuGlView::uploadTexture(const unsigned char* rgba, unsigned int wid,
                              unsigned int hei) {
  if (!rgba || wid == 0 || hei == 0) {
    return;
  }

  if (tex_id_ == 0) {
    glGenTextures(1, &tex_id_);
  }

  glBindTexture(GL_TEXTURE_2D, tex_id_);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

  if (wid != tex_w_ || hei != tex_h_) {
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, static_cast<GLsizei>(wid),
                 static_cast<GLsizei>(hei), 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    tex_w_ = wid;
    tex_h_ = hei;
  } else {
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, static_cast<GLsizei>(wid),
                    static_cast<GLsizei>(hei), GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  }
}

void EmuGlView::refreshFrame() {
  if (!framebuffer_) {
    return;
  }

  const unsigned char* rgba = nullptr;
  unsigned int wid = 0;
  unsigned int hei = 0;
  uint64_t serial = 0;
  if (!framebuffer_->Acquire(&rgba, &wid, &hei, &serial)) {
    return;
  }
  if (serial == last_serial_ && !frame_dirty_) {
    return;
  }

  const size_t bytes = static_cast<size_t>(wid) * hei * 4;
  staging_rgba_.resize(bytes);
  std::memcpy(staging_rgba_.data(), rgba, bytes);

  emu_wid_ = static_cast<int>(wid);
  emu_hei_ = static_cast<int>(hei);
  pending_w_ = wid;
  pending_h_ = hei;
  pending_serial_ = serial;
  frame_dirty_ = true;
  update();
}

void EmuGlView::drawTexturedQuad(int x, int y, int dst_w, int dst_h, int vp_w,
                                 int vp_h) {
  if (!program_.isLinked()) {
    return;
  }

  program_.bind();
  program_.setUniformValue("uTex", 0);
  program_.setUniformValue("uWin",
                           QVector2D(static_cast<float>(vp_w), static_cast<float>(vp_h)));
  program_.setUniformValue(
      "uRect", QVector4D(static_cast<float>(x), static_cast<float>(y),
                         static_cast<float>(dst_w), static_cast<float>(dst_h)));

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, tex_id_);

  vbo_.bind();
  program_.enableAttributeArray(0);
  program_.enableAttributeArray(1);
  program_.setAttributeBuffer(0, GL_FLOAT, 0, 2, 4 * sizeof(float));
  program_.setAttributeBuffer(1, GL_FLOAT, 2 * sizeof(float), 2, 4 * sizeof(float));
  glDrawArrays(GL_TRIANGLES, 0, 6);
  program_.disableAttributeArray(0);
  program_.disableAttributeArray(1);
  vbo_.release();
  program_.release();
}

void EmuGlView::drawImeOverlay() {
  if (ime_preedit_.isEmpty()) {
    return;
  }
  QPainter painter(this);
  const QRect bar(0, height() - kImeInlineBarHeight, width(), kImeInlineBarHeight);
  painter.fillRect(bar, QColor(20, 24, 40, 220));
  painter.setPen(QColor(120, 180, 255));
  painter.drawRect(bar);
  painter.setPen(Qt::white);
  painter.drawText(bar.adjusted(8, 0, -8, 0), Qt::AlignVCenter | Qt::AlignLeft, ime_preedit_);
}

void EmuGlView::paintGL() {
  const qreal dpr = devicePixelRatioF();
  const int vp_w = std::max(1, static_cast<int>(std::lround(width() * dpr)));
  const int vp_h = std::max(1, static_cast<int>(std::lround(height() * dpr)));
  glViewport(0, 0, vp_w, vp_h);
  glDisable(GL_BLEND);
  glClear(GL_COLOR_BUFFER_BIT);

  if (frame_dirty_ && !staging_rgba_.empty() && pending_w_ > 0 && pending_h_ > 0) {
    uploadTexture(staging_rgba_.data(), pending_w_, pending_h_);
    frame_dirty_ = false;
    last_serial_ = pending_serial_;
  }

  if (tex_id_ != 0 && emu_wid_ > 0 && emu_hei_ > 0) {
    const int dst_w =
        std::max(1, static_cast<int>(std::lround(emu_wid_ * scale_ * dpr)));
    const int dst_h =
        std::max(1, static_cast<int>(std::lround(emu_hei_ * scale_ * dpr)));
    const int box_h = force480_layout_
                          ? std::max(dst_h, static_cast<int>(std::lround(480 * scale_ * dpr)))
                          : dst_h;
    const int x = (vp_w - dst_w) / 2;
    const int y = (vp_h - box_h) / 2 + (box_h - dst_h) / 2;
    drawTexturedQuad(x, y, dst_w, dst_h, vp_w, vp_h);
  }

  drawImeOverlay();
}
