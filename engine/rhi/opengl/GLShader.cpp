#include "GLShader.h"
#include "engine/debug/DebugListenBus.h"
#include <vector>

namespace ark {

GLShader::GLShader() = default;

GLShader::~GLShader() {
    if (program_) {
        glDeleteProgram(program_);
    }
}

bool GLShader::Compile(const std::string& vertexSrc, const std::string& fragmentSrc) {
    GLuint vs = CompileStage(GL_VERTEX_SHADER, vertexSrc);
    if (!vs) return false;

    GLuint fs = CompileStage(GL_FRAGMENT_SHADER, fragmentSrc);
    if (!fs) {
        glDeleteShader(vs);
        return false;
    }

    program_ = glCreateProgram();
    glAttachShader(program_, vs);
    glAttachShader(program_, fs);
    glLinkProgram(program_);

    GLint success = 0;
    glGetProgramiv(program_, GL_LINK_STATUS, &success);
    if (!success) {
        GLint logLen = 0;
        glGetProgramiv(program_, GL_INFO_LOG_LENGTH, &logLen);
        std::vector<char> log(static_cast<size_t>(logLen));
        glGetProgramInfoLog(program_, logLen, nullptr, log.data());
        ARK_LOG_ERROR("RHI", std::string("Shader link failed: ") + log.data());
        glDeleteProgram(program_);
        program_ = 0;
    }

    glDeleteShader(vs);
    glDeleteShader(fs);
    return program_ != 0;
}

void GLShader::Bind() const {
    glUseProgram(program_);
}

void GLShader::SetUniformMat4(const std::string& name, const float* value) {
    Bind();
    glUniformMatrix4fv(GetUniformLocation(name), 1, GL_FALSE, value);
}

void GLShader::SetUniformVec3(const std::string& name, const float* value) {
    Bind();
    glUniform3fv(GetUniformLocation(name), 1, value);
}

void GLShader::SetUniformVec4(const std::string& name, const float* value) {
    Bind();
    glUniform4fv(GetUniformLocation(name), 1, value);
}

void GLShader::SetUniformFloat(const std::string& name, float value) {
    Bind();
    glUniform1f(GetUniformLocation(name), value);
}

void GLShader::SetUniformInt(const std::string& name, int value) {
    Bind();
    glUniform1i(GetUniformLocation(name), value);
}

GLuint GLShader::CompileStage(GLenum stage, const std::string& source) {
    GLuint shader = glCreateShader(stage);
    const char* src = source.c_str();
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    GLint success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        GLint logLen = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLen);
        std::vector<char> log(static_cast<size_t>(logLen));
        glGetShaderInfoLog(shader, logLen, nullptr, log.data());
        const char* stageName = (stage == GL_VERTEX_SHADER) ? "vertex" : "fragment";
        ARK_LOG_ERROR("RHI", std::string("Shader compile failed (") + stageName + "): " + log.data());
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

GLint GLShader::GetUniformLocation(const std::string& name) {
    return glGetUniformLocation(program_, name.c_str());
}

} // namespace ark
