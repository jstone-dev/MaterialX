#include <MaterialXView/Material.h>

#include <MaterialXGenShader/DefaultColorManagementSystem.h>
#include <MaterialXGenShader/Util.h>
#include <MaterialXGenGlsl/GlslShaderGenerator.h>
#include <MaterialXGenShader/HwShader.h>

#include <iostream>
#include <unordered_set>

using MatrixXfProxy = Eigen::Map<const ng::MatrixXf>;
using MatrixXuProxy = Eigen::Map<const ng::MatrixXu>;

void loadLibraries(const mx::StringVec& libraryFolders, const mx::FileSearchPath& searchPath, mx::DocumentPtr doc)
{
    const std::string MTLX_EXTENSION("mtlx");
    for (const std::string& libraryFolder : libraryFolders)
    {
        mx::FilePath path = searchPath.find(libraryFolder);
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

    doc = mx::createDocument();
    mx::readFromXmlFile(doc, filePath);
    doc->importLibrary(stdLib);

    // Scan for a list of elements which can be rendered
    mx::findRenderableElements(doc, elements); 
}

void remapNodes(mx::DocumentPtr& doc, const mx::StringMap& nodeRemap)
{
    for (mx::ElementPtr elem : doc->traverseTree())
    {
        mx::NodePtr node = elem->asA<mx::Node>();
        mx::ShaderRefPtr shaderRef = elem->asA<mx::ShaderRef>();
        if (node && nodeRemap.count(node->getCategory()))
        {
            node->setCategory(nodeRemap.at(node->getCategory()));
        }
        if (shaderRef)
        {
            mx::NodeDefPtr nodeDef = shaderRef->getNodeDef();
            if (nodeDef && nodeRemap.count(nodeDef->getNodeString()))
            {
                shaderRef->setNodeString(nodeRemap.at(nodeDef->getNodeString()));
                shaderRef->removeAttribute(mx::ShaderRef::NODE_DEF_ATTRIBUTE);
            }
        }
    }
}

StringPair generateSource(const mx::FileSearchPath& searchPath, mx::HwShaderPtr& hwShader, mx::ElementPtr elem)
{  
    if (!elem)
    {
        return StringPair();
    }

    mx::ShaderGeneratorPtr shaderGenerator = mx::GlslShaderGenerator::create();
    mx::DefaultColorManagementSystemPtr cms = mx::DefaultColorManagementSystem::create(shaderGenerator->getLanguage());
    cms->loadLibrary(elem->getDocument());
    for (int i = 0; i < searchPath.size(); i++)
    {
        // TODO: The registerSourceCodeSearchPath method should probably take a
        //       full FileSearchPath rather than a single FilePath.
        shaderGenerator->registerSourceCodeSearchPath(searchPath[i]);
    }
    shaderGenerator->setColorManagementSystem(cms);

    mx::GenOptions options;
    options.hwTransparency = isTransparentSurface(elem, *shaderGenerator);
    options.targetColorSpaceOverride = "lin_rec709";
    mx::ShaderPtr sgShader = shaderGenerator->generate("Shader", elem, options);

    std::string vertexShader = sgShader->getSourceCode(mx::HwShader::VERTEX_STAGE);
    std::string pixelShader = sgShader->getSourceCode(mx::HwShader::PIXEL_STAGE);

    hwShader = std::dynamic_pointer_cast<mx::HwShader>(sgShader);

    return StringPair(vertexShader, pixelShader);
}

MaterialPtr Material::generateShader(const mx::FileSearchPath& searchPath, mx::ElementPtr elem)
{
    mx::HwShaderPtr hwShader;
    StringPair source = generateSource(searchPath, hwShader, elem);
    if (!source.first.empty() && !source.second.empty())
    {
        GLShaderPtr ngShader = GLShaderPtr(new ng::GLShader());
        ngShader->init(elem->getNamePath(), source.first, source.second);

        MaterialPtr material = MaterialPtr(new Material(ngShader, hwShader));
        material->_hasTransparency = hwShader ? hwShader->hasTransparency() : false;

        return material;
    }
    return nullptr;
}

void Material::bindMesh(MeshPtr& mesh)
{
    if (!mesh || !_ngShader)
    {
        return;
    }

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

bool Material::bindImage(const std::string& filename, const std::string& uniformName, 
                         mx::GLTextureHandlerPtr imageHandler, mx::ImageDesc& desc)
{
    if (!_ngShader)
    {
        return false;
    }

    _ngShader->bind();

    // Acquire the given image.
    std::array<float, 4> defaultColor{0, 0, 0, 1};
    if (!imageHandler->acquireImage(filename, desc, true, &defaultColor))
    {
        std::cerr << "Failed to load image: " << filename << std::endl;
        return false;
    }

    // Bind the image and set its sampling properties.
    _ngShader->setUniform(uniformName, desc.resourceId);
    mx::ImageSamplingProperties samplingProperties;
    imageHandler->bindImage(filename, samplingProperties);

    return true;
}

void Material::bindImages(mx::GLTextureHandlerPtr imageHandler, const mx::FileSearchPath& searchPath)
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
            filename = searchPath.find(uniform->value->getValueString());
        }

        mx::ImageDesc desc;
        bindImage(filename, uniformName, imageHandler, desc);
    }
}

void Material::bindLights(mx::GLTextureHandlerPtr imageHandler, const mx::FileSearchPath& imagePath, int envSamples)
{
    if (!_ngShader)
    {
        return;
    }

    _ngShader->bind();

    // Bind light properties.
    if (_ngShader->uniform("u_envSamples", false) != -1)
    {
        _ngShader->setUniform("u_envSamples", envSamples);
    }
    mx::StringMap lightTextures = {
        { "u_envRadiance", "documents/TestSuite/Images/san_giuseppe_bridge.exr" },
        { "u_envIrradiance", "documents/TestSuite/Images/san_giuseppe_bridge_diffuse.exr" }
    };
    for (auto pair : lightTextures)
    {
        if (_ngShader->uniform(pair.first, false) != -1)
        {
            // Access cached image or load from disk.
            mx::FilePath path = imagePath.find(pair.second);
            const std::string filename = path.asString();

            mx::ImageDesc desc;
            if (bindImage(filename, pair.first, imageHandler, desc))
            {
                // Bind any associated uniforms.
                if (pair.first == "u_envRadiance")
                {
                    _ngShader->setUniform("u_envRadianceMips", desc.mipCount);
                }
            }
        }
    }
}

void Material::bindViewInformation(const mx::Matrix44& world, const mx::Matrix44& view, const mx::Matrix44& proj)
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

    
}

mx::Shader::Variable* Material::findUniform(const std::string path) const
{
    GLShaderPtr shader = ngShader();
    mx::HwShaderPtr hwShader = mxShader();
    if (hwShader && shader)
    {
        const MaterialX::Shader::VariableBlock publicUniforms = hwShader->getUniformBlock(MaterialX::Shader::PIXEL_STAGE, MaterialX::Shader::PUBLIC_UNIFORMS);
        for (auto uniform : publicUniforms.variableOrder)
        {
            if (uniform->path == path)
            {
                return uniform;
            }
        }
    }
    return nullptr;
}

