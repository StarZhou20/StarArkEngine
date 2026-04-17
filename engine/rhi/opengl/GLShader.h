#pragma once

#include "engine/rhi/RHIShader.h"
#include <GL/glew.h>

namespace ark {

class GLShader : public RHIShader {
public:
    GLShader();
    ~GLShader() override;

    GLShader(const GLShader&) = delete;
    GLShader& operator=(const GLShader&) = delete;

    bool Compile(const std::string& vertexSrc, const std::string& fragmentSrc) override;
    void SetUniformMat4(const std::string& name, const float* value) override;
    void SetUniformVec3(const std::string& name, const float* value) override;
    void SetUniformVec4(const std::string& name, const float* value) override;
    void SetUniformFloat(const std::string& name, float value) override;
    void SetUniformInt(const std::string& name, int value) override;

    GLuint GetProgram() const { return program_; }
    void Bind() const;

private:
    GLuint CompileStage(GLenum stage, const std::string& source);
    GLint GetUniformLocation(const std::string& name);

    GLuint program_ = 0;
};

} // namespace ark
