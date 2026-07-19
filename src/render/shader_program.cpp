#include "render/shader_program.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

namespace {
std::string readTextFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Failed to open shader: " << path << '\n';
        return {};
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

GLuint compile(GLenum stage, const std::string& source, const char* label) {
    GLuint shader = glCreateShader(stage);
    const char* src = source.c_str();
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok == GL_TRUE) {
        return shader;
    }

    GLint len = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &len);
    std::vector<char> log(static_cast<size_t>(len > 1 ? len : 1));
    glGetShaderInfoLog(shader, len, nullptr, log.data());
    std::cerr << "Shader compile failed (" << label << "): " << log.data() << '\n';

    glDeleteShader(shader);
    return 0;
}
}  // namespace

ShaderProgram::~ShaderProgram() {
    if (program_) {
        glDeleteProgram(program_);
    }
}

ShaderProgram::ShaderProgram(ShaderProgram&& other) noexcept {
    program_ = other.program_;
    other.program_ = 0;
}

ShaderProgram& ShaderProgram::operator=(ShaderProgram&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    if (program_) {
        glDeleteProgram(program_);
    }
    program_ = other.program_;
    other.program_ = 0;
    return *this;
}

bool ShaderProgram::linkProgram(GLuint vs, GLuint fs, GLuint cs) {
    if (program_) {
        glDeleteProgram(program_);
    }

    program_ = glCreateProgram();

    if (vs) {
        glAttachShader(program_, vs);
    }
    if (fs) {
        glAttachShader(program_, fs);
    }
    if (cs) {
        glAttachShader(program_, cs);
    }

    glLinkProgram(program_);

    GLint ok = GL_FALSE;
    glGetProgramiv(program_, GL_LINK_STATUS, &ok);

    if (vs) {
        glDetachShader(program_, vs);
        glDeleteShader(vs);
    }
    if (fs) {
        glDetachShader(program_, fs);
        glDeleteShader(fs);
    }
    if (cs) {
        glDetachShader(program_, cs);
        glDeleteShader(cs);
    }

    if (ok == GL_TRUE) {
        return true;
    }

    GLint len = 0;
    glGetProgramiv(program_, GL_INFO_LOG_LENGTH, &len);
    std::vector<char> log(static_cast<size_t>(len > 1 ? len : 1));
    glGetProgramInfoLog(program_, len, nullptr, log.data());
    std::cerr << "Program link failed: " << log.data() << '\n';
    return false;
}

bool ShaderProgram::loadComputeFromFile(const std::string& computePath) {
    const std::string compSource = readTextFile(computePath);
    if (compSource.empty()) {
        return false;
    }

    const GLuint cs = compile(GL_COMPUTE_SHADER, compSource, "compute");
    if (!cs) {
        return false;
    }

    return linkProgram(0, 0, cs);
}

bool ShaderProgram::loadGraphicsFromFiles(const std::string& vertexPath, const std::string& fragmentPath) {
    const std::string vertSource = readTextFile(vertexPath);
    const std::string fragSource = readTextFile(fragmentPath);

    if (vertSource.empty() || fragSource.empty()) {
        return false;
    }

    const GLuint vs = compile(GL_VERTEX_SHADER, vertSource, "vertex");
    const GLuint fs = compile(GL_FRAGMENT_SHADER, fragSource, "fragment");

    if (!vs || !fs) {
        if (vs) {
            glDeleteShader(vs);
        }
        if (fs) {
            glDeleteShader(fs);
        }
        return false;
    }

    return linkProgram(vs, fs, 0);
}

void ShaderProgram::use() const {
    glUseProgram(program_);
}
