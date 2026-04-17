#pragma once

#include <string>

namespace ark {

class RHIShader {
public:
    virtual ~RHIShader() = default;
    virtual bool Compile(const std::string& vertexSrc, const std::string& fragmentSrc) = 0;
    virtual void SetUniformMat4(const std::string& name, const float* value) = 0;
    virtual void SetUniformVec3(const std::string& name, const float* value) = 0;
    virtual void SetUniformVec4(const std::string& name, const float* value) = 0;
    virtual void SetUniformFloat(const std::string& name, float value) = 0;
    virtual void SetUniformInt(const std::string& name, int value) = 0;
};

} // namespace ark
