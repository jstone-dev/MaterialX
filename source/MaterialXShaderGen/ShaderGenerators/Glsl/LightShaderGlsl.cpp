#include <MaterialXShaderGen/ShaderGenerators/Glsl/LightShaderGlsl.h>
#include <MaterialXShaderGen/Util.h>

namespace MaterialX
{

SgImplementationPtr LightShaderGlsl::create()
{
    return std::make_shared<LightShaderGlsl>();
}

const string& LightShaderGlsl::getLanguage() const
{
    return GlslShaderGenerator::LANGUAGE;
}

const string& LightShaderGlsl::getTarget() const
{
    return GlslShaderGenerator::TARGET;
}

void LightShaderGlsl::initialize(ElementPtr implementation, ShaderGenerator& shadergen)
{
    SourceCode::initialize(implementation, shadergen);

    if (_inlined)
    {
        throw ExceptionShaderGenError("Light shaders doesn't support inlined implementations'");
    }

    ImplementationPtr impl = implementation->asA<Implementation>();
    if (!impl)
    {
        throw ExceptionShaderGenError("Element '" + implementation->getName() + "' is not an Implementation element");
    }

    // Create light uniforms for all inputs and parameters on the nodedef
    NodeDefPtr nodeDef = impl->getNodeDef();
    _lightUniforms.resize(nodeDef->getInputCount() + nodeDef->getParameterCount());
    size_t index = 0;
    for (InputPtr input : nodeDef->getInputs())
    {
        _lightUniforms[index++] = Shader::Variable(input->getType(), input->getName(), EMPTY_STRING, input->getValue());
    }
    for (ParameterPtr param : nodeDef->getParameters())
    {
        _lightUniforms[index++] = Shader::Variable(param->getType(), param->getName(), EMPTY_STRING, param->getValue());
    }
}

void LightShaderGlsl::createVariables(const SgNode& /*node*/, ShaderGenerator& /*shadergen*/, Shader& shader_)
{
    HwShader& shader = static_cast<HwShader&>(shader_);

    // Create variables used by this shader
    for (const Shader::Variable& uniform : _lightUniforms)
    {
        shader.createUniform(HwShader::PIXEL_STAGE, HwShader::LIGHT_DATA_BLOCK, uniform.type, uniform.name, uniform.semantic, uniform.value);
    }

    // Create uniform for number of active light sources
    shader.createUniform(HwShader::PIXEL_STAGE, HwShader::PRIVATE_UNIFORMS, DataType::INTEGER, "u_numActiveLightSources",
        EMPTY_STRING, Value::createValue<int>(0));
}

void LightShaderGlsl::emitFunctionCall(const SgNode& /*node*/, ShaderGenerator& /*shadergen*/, Shader& shader_)
{
    HwShader& shader = static_cast<HwShader&>(shader_);

    BEGIN_SHADER_STAGE(shader, HwShader::PIXEL_STAGE)

        shader.addLine(_functionName + "(light, position, result)");

    END_SHADER_STAGE(shader, HwShader::PIXEL_STAGE)
}

} // namespace MaterialX
