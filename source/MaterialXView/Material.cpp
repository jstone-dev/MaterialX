#include <MaterialXView/Material.h>

#include <MaterialXGenShader/Util.h>
#include <MaterialXGenGlsl/GlslShaderGenerator.h>
#include <MaterialXGenShader/HwShader.h>

#include <iostream>
#include <unordered_set>

using MatrixXfProxy = Eigen::Map<const ng::MatrixXf>;
using MatrixXuProxy = Eigen::Map<const ng::MatrixXu>;

void loadLibraries(const mx::StringVec& libraryNames, const mx::FilePath& searchPath, mx::DocumentPtr doc)
{
    const std::string MTLX_EXTENSION("mtlx");
    for (const std::string& library : libraryNames)
    {
        mx::FilePath path = searchPath / library;
        mx::StringVec filenames;
        mx::getFilesInDirectory(path.asString(), filenames, MTLX_EXTENSION);

        for (const std::string& filename : filenames)
        {
            mx::FilePath file = path / filename;
            mx::DocumentPtr libDoc = mx::createDocument();
            mx::readFromXmlFile(libDoc, file);
            libDoc->setSourceUri(file);
            mx::CopyOptions copyOptions;
            copyOptions.skipDuplicateElements = true;
            doc->importLibrary(libDoc, &copyOptions);
        }
    }
}

void loadDocument(const mx::FilePath& filePath, mx::DocumentPtr& doc, mx::DocumentPtr stdLib, std::vector<mx::TypedElementPtr>& elements)
{
    elements.clear();

    try
    {
        doc = mx::createDocument();
        mx::readFromXmlFile(doc, filePath);
        doc->importLibrary(stdLib);
    }
    catch (std::exception& e)
    {
        throw e;
    }

    // Scan for a list of elements which can be rendered
    mx::findRenderableElements(doc, elements); 
}

StringPair generateSource(const mx::FilePath& searchPath, mx::HwShaderPtr& hwShader, mx::ElementPtr elem)
{  
    if (!elem)
    {
        return StringPair();
    }

    mx::ShaderGeneratorPtr shaderGenerator = mx::GlslShaderGenerator::create();
    shaderGenerator->registerSourceCodeSearchPath(searchPath);

    mx::GenOptions options;
    options.hwTransparency = isTransparentSurface(elem, *shaderGenerator);
    mx::ShaderPtr sgShader = shaderGenerator->generate("Shader", elem, options);

    std::string vertexShader = sgShader->getSourceCode(mx::HwShader::VERTEX_STAGE);
    std::string pixelShader = sgShader->getSourceCode(mx::HwShader::PIXEL_STAGE);

    hwShader = std::dynamic_pointer_cast<mx::HwShader>(sgShader);

    return StringPair(vertexShader, pixelShader);
}

MaterialPtr Material::generateShader(const mx::FilePath& searchPath, mx::ElementPtr elem)
{
    mx::HwShaderPtr hwShader = nullptr;
    StringPair source = generateSource(searchPath, hwShader, elem);
    if (!source.first.empty() && !source.second.empty())
    {
        GLShaderPtr ngShader = GLShaderPtr(new ng::GLShader());
        ngShader->init(elem->getNamePath(), source.first, source.second);

        MaterialPtr shader = MaterialPtr(new Material(ngShader, hwShader));
        shader->_hasTransparency = hwShader ? hwShader->hasTransparency() : false;

        return shader;
    }
    return nullptr;
}

void Material::bindMesh(MeshPtr& mesh)
{
    if (!mesh || !_ngShader)
    {
        return;
    }

    // TODO: This needs to be reversed to examine the MaterialX shader for required attributes
    _ngShader->bind();
    if (_ngShader->attrib("i_position") != -1)
    {
        MatrixXfProxy positions(&mesh->getPositions()[0][0], 3, mesh->getPositions().size());
        _ngShader->uploadAttrib("i_position", positions);
    }
    if (_ngShader->attrib("i_normal", false) != -1)
    {
        MatrixXfProxy normals(&mesh->getNormals()[0][0], 3, mesh->getNormals().size());
        _ngShader->uploadAttrib("i_normal", normals);
    }
    if (_ngShader->attrib("i_tangent", false) != -1)
    {
        MatrixXfProxy tangents(&mesh->getTangents()[0][0], 3, mesh->getTangents().size());
        _ngShader->uploadAttrib("i_tangent", tangents);
    }
    if (_ngShader->attrib("i_texcoord_0", false) != -1)
    {
        MatrixXfProxy texcoords(&mesh->getTexcoords()[0][0], 2, mesh->getTexcoords().size());
        _ngShader->uploadAttrib("i_texcoord_0", texcoords);
    }
    MatrixXuProxy indices(&mesh->getIndices()[0], 3, mesh->getIndices().size() / 3);
    _ngShader->uploadIndices(indices);
}

bool Material::acquireTexture(const std::string& filename, mx::GLTextureHandlerPtr imageHandler, mx::ImageDesc& desc)
{
    // Load the requested texture into memory.
    std::string fileName(filename);
    if (!imageHandler->acquireImage(fileName, desc, true))
    {
        std::cerr << "Failed to load image: " << filename << std::endl;
        return false;
    }
    return true;
}

bool Material::bindTexture(const std::string& filename, const std::string& uniformName, 
                           mx::GLTextureHandlerPtr imageHandler, mx::ImageDesc& desc)
{
    if (!_ngShader)
    {
        return false;
    }

    _ngShader->bind();

    acquireTexture(filename, imageHandler, desc);

    // Bind texture to shader.
    _ngShader->setUniform(uniformName, desc.resourceId);

    mx::ImageSamplingProperties samplingProperties;
    samplingProperties.filterType = -1;
    samplingProperties.uaddressMode = -1;
    samplingProperties.vaddressMode = -1;

    imageHandler->bindImage(filename, samplingProperties);

    return true;
}

void Material::bindTextures(mx::GLTextureHandlerPtr imageHandler, mx::FilePath imagePath)
{
    mx::HwShaderPtr hwShader = mxShader();
    const MaterialX::Shader::VariableBlock publicUniforms = hwShader->getUniformBlock(MaterialX::Shader::PIXEL_STAGE, MaterialX::Shader::PUBLIC_UNIFORMS);
    for (auto uniform : publicUniforms.variableOrder)
    {
        if (uniform->type != MaterialX::Type::FILENAME)
        {
            continue;
        }
        const std::string& uniformName = uniform->name;
        std::string filename;
        if (uniform->value)
        {
            mx::FilePath uniformPath(uniform->value->getValueString());
            if (uniformPath.isAbsolute())
            {
                filename = uniformPath;
            }
            else
            {
                filename = imagePath / uniformPath;
            }
        }

        mx::ImageDesc desc;
        bindTexture(filename, uniformName, imageHandler, desc);
    }
}

void Material::bindUniforms(mx::GLTextureHandlerPtr imageHandler, mx::FilePath imagePath, int envSamples,
                            mx::Matrix44& world, mx::Matrix44& view, mx::Matrix44& proj)
{
    GLShaderPtr shader = ngShader();
    mx::HwShaderPtr hwShader = mxShader();

    mx::Matrix44 viewProj = proj * view;
    mx::Matrix44 invView = view.getInverse();
    mx::Matrix44 invTransWorld = world.getInverse().getTranspose();

    // Bind view properties.
    shader->setUniform("u_worldMatrix", ng::Matrix4f(world.getTranspose().data()));
    shader->setUniform("u_viewProjectionMatrix", ng::Matrix4f(viewProj.getTranspose().data()));
    if (shader->uniform("u_worldInverseTransposeMatrix", false) != -1)
    {
        shader->setUniform("u_worldInverseTransposeMatrix", ng::Matrix4f(invTransWorld.getTranspose().data()));
    }
    if (shader->uniform("u_viewPosition", false) != -1)
    {
        mx::Vector3 viewPosition(invView[0][3], invView[1][3], invView[2][3]);
        shader->setUniform("u_viewPosition", ng::Vector3f(viewPosition.data()));
    }

    // Bind surface textures
    bindTextures(imageHandler, imagePath);

    // Bind light properties.
    if (shader->uniform("u_envSamples", false) != -1)
    {
        shader->setUniform("u_envSamples", envSamples);
    }
    mx::StringMap lightTextures = {
        {"u_envRadiance", "documents/TestSuite/Images/san_giuseppe_bridge.exr" },
        {"u_envIrradiance", "documents/TestSuite/Images/san_giuseppe_bridge_diffuse.exr" }
    };
    for (auto pair : lightTextures)
    {
        if (shader->uniform(pair.first, false) != -1)
        {
            // Access cached image or load from disk.
            mx::FilePath path = imagePath / mx::FilePath(pair.second);
            const std::string filename = path.asString();

            mx::ImageDesc desc;
            if (bindTexture(filename, pair.first, imageHandler, desc))
            {
                // Bind any associated uniforms.
                if (pair.first == "u_envRadiance")
                {
                    shader->setUniform("u_envRadianceMips", desc.mipCount);
                }
            }
        }
    }
}
