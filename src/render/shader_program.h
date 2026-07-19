#pragma once

#include <glad/gl.h>

#include <string>

class ShaderProgram {
public:
    ShaderProgram() = default;
    ~ShaderProgram();

    ShaderProgram(const ShaderProgram&) = delete;
    ShaderProgram& operator=(const ShaderProgram&) = delete;

    ShaderProgram(ShaderProgram&& other) noexcept;
    ShaderProgram& operator=(ShaderProgram&& other) noexcept;

    bool loadComputeFromFile(const std::string& computePath);
    bool loadGraphicsFromFiles(const std::string& vertexPath, const std::string& fragmentPath);

    void use() const;
    GLuint id() const { return program_; }
    bool isValid() const { return program_ != 0; }

private:
    bool linkProgram(GLuint vs, GLuint fs, GLuint cs);

    GLuint program_ = 0;
};
