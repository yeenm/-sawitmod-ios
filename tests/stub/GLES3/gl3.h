#pragma once
// 语法自检用的 GLES3/gl3.h（Windows 没有）。
// 只声明 Menu.cpp / CyloneUI.h 用到的类型与函数，用于 MSVC 语法检查。
#include <cstddef>
#include <cstdint>

typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef void GLvoid;
typedef int GLint;
typedef unsigned int GLuint;
typedef int GLsizei;
typedef float GLfloat;
typedef char GLchar;
typedef ptrdiff_t GLintptr;
typedef ptrdiff_t GLsizeiptr;

#define GL_FALSE 0
#define GL_TRUE 1
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_LINEAR 0x2601
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
#define GL_RGBA8 0x8058
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STREAM_DRAW 0x88E0
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_BLEND 0x0BE2
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_FUNC_ADD 0x8006
#define GL_SCISSOR_TEST 0x0C11
#define GL_DEPTH_TEST 0x0B71
#define GL_CULL_FACE 0x0B44
#define GL_FRAMEBUFFER 0x8D40
#define GL_RENDERBUFFER 0x8D41
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_COLOR_BUFFER_BIT 0x00004000

inline void glGenTextures(GLsizei, GLuint *) {}
inline void glDeleteTextures(GLsizei, const GLuint *) {}
inline void glBindTexture(GLenum, GLuint) {}
inline void glTexParameteri(GLenum, GLenum, GLint) {}
inline void glTexImage2D(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,
                         const void *) {}
inline void glGenBuffers(GLsizei, GLuint *) {}
inline void glBindBuffer(GLenum, GLuint) {}
inline void glBufferData(GLenum, GLsizeiptr, const void *, GLenum) {}
inline void glBufferSubData(GLenum, GLintptr, GLsizeiptr, const void *) {}
inline void glDeleteBuffers(GLsizei, const GLuint *) {}
inline GLuint glCreateShader(GLenum) { return 0; }
inline void glShaderSource(GLuint, GLsizei, const GLchar *const *, const GLint *) {}
inline void glCompileShader(GLuint) {}
inline void glGetShaderiv(GLuint, GLenum, GLint *) {}
inline GLuint glCreateProgram() { return 0; }
inline void glAttachShader(GLuint, GLuint) {}
inline void glLinkProgram(GLuint) {}
inline void glGetProgramiv(GLuint, GLenum, GLint *) {}
inline void glUseProgram(GLuint) {}
inline void glDeleteProgram(GLuint) {}
inline void glDeleteShader(GLuint) {}
inline GLint glGetUniformLocation(GLuint, const GLchar *) { return 0; }
inline void glUniform1i(GLint, GLint) {}
inline void glEnable(GLenum) {}
inline void glDisable(GLenum) {}
inline void glBlendEquation(GLenum) {}
inline void glBlendFuncSeparate(GLenum, GLenum, GLenum, GLenum) {}
inline void glEnableVertexAttribArray(GLuint) {}
inline void glVertexAttribPointer(GLuint, GLint, GLenum, GLboolean, GLsizei, const void *) {}
inline void glDrawElements(GLenum, GLsizei, GLenum, const void *) {}
inline void glDrawElementsBaseVertex(GLenum, GLsizei, GLenum, const void *, GLint) {}
inline void glViewport(GLint, GLint, GLsizei, GLsizei) {}
inline void glScissor(GLint, GLint, GLsizei, GLsizei) {}
inline void glClearColor(GLfloat, GLfloat, GLfloat, GLfloat) {}
inline void glClear(GLbitfield) {}
inline void glGenFramebuffers(GLsizei, GLuint *) {}
inline void glBindFramebuffer(GLenum, GLuint) {}
inline void glGenRenderbuffers(GLsizei, GLuint *) {}
inline void glBindRenderbuffer(GLenum, GLuint) {}
inline void glFramebufferRenderbuffer(GLenum, GLenum, GLenum, GLuint) {}
inline void glGetRenderbufferParameteriv(GLenum, GLenum, GLint *) {}
inline GLenum glCheckFramebufferStatus(GLenum) { return GL_FRAMEBUFFER_COMPLETE; }
inline void glGetIntegerv(GLenum, GLint *) {}
inline void glGetShaderInfoLog(GLuint, GLsizei, GLsizei *, GLchar *) {}
inline void glGetProgramInfoLog(GLuint, GLsizei, GLsizei *, GLchar *) {}
inline void glGetTexLevelParameteriv(GLenum, GLint, GLenum, GLint *) {}
