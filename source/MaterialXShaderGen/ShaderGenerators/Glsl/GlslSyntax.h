#ifndef MATERIALX_GLSL_SYNTAX_H
#define MATERIALX_GLSL_SYNTAX_H

#include <MaterialXShaderGen/Syntax.h>

namespace MaterialX
{

/// Syntax class for GLSL (OpenGL Shading Language)
class GlslSyntax : public Syntax
{
public:
    GlslSyntax();

    static SyntaxPtr create() { return std::make_shared<GlslSyntax>(); }

    string getValue(const Value& value, const string& type, bool paramInit = false) const override;
};

} // namespace MaterialX

#endif
