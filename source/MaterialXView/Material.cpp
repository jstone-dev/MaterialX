#include <MaterialXView/Material.h>

#include <MaterialXGenGlsl/GlslShaderGenerator.h>
#include <MaterialXGenShader/DefaultColorManagementSystem.h>
#include <MaterialXGenShader/HwShader.h>
#include <MaterialXGenShader/Util.h>

#include <iostream>

using MatrixXfProxy = Eigen::Map<const ng::MatrixXf>;
using MatrixXuProxy = Eigen::Map<const ng::MatrixXu>;

mx::DocumentPtr loadDocument(const mx::FilePath& filePath, mx::DocumentPtr stdLib)
{
    mx::DocumentPtr doc = mx::createDocument();
    mx::readFromXmlFile(doc, filePath);
    mx::CopyOptions copyOptions;
    copyOptions.skipDuplicateElements = true;
    doc->importLibrary(stdLib, &copyOptions);

    return doc;
}

mx::DocumentPtr loadLibraries(const mx::StringVec& libraryFolders, const mx::FileSearchPath& searchPath)
{
    mx::DocumentPtr doc = mx::createDocument();
    for (const std::string& libraryFolder : libraryFolders)
    {
        mx::FilePath path = searchPath.find(libraryFolder);
        mx::StringVec filenames;
        mx::getFilesInDirectory(path.asString(), filenames, "mtlx");

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
    return doc;
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

mx::HwShaderPtr generateSource(const mx::FileSearchPath& searchPath, mx::ElementPtr elem)
{  
    if (!elem)
    {
        return nullptr;
    }

    mx::ShaderGeneratorPtr shaderGenerator = mx::GlslShaderGenerator::create();
    mx::DefaultColorManagementSystemPtr cms = mx::DefaultColorManagementSystem::create(shaderGenerator->getLanguage());
    cms->loadLibrary(elem->getDocument());
    for (size_t i = 0; i < searchPath.size(); i++)
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

    return std::dynamic_pointer_cast<mx::HwShader>(sgShader);
}

//
// Viewer methods
//

MaterialPtr Material::generateMaterial(const mx::FileSearchPath& searchPath, mx::ElementPtr elem)
{
    mx::HwShaderPtr hwShader = generateSource(searchPath, elem);
    if (!hwShader)
    {
        return nullptr;
    }

    std::string vertexShader = hwShader->getSourceCode(mx::HwShader::VERTEX_STAGE);
    std::string pixelShader = hwShader->getSourceCode(mx::HwShader::PIXEL_STAGE);

    GLShaderPtr glShader = std::make_shared<ng::GLShader>();
    glShader->init(elem->getNamePath(), vertexShader, pixelShader);
    return MaterialPtr(new Material(glShader, hwShader));
}

void Material::assignPartitionsToMaterial(const mx::GeometryHandler handler)
{
    _geometryList.clear();
    for (auto mesh : handler.getMeshes())
    {
        for (size_t p = 0; p < mesh->getPartitionCount(); p++)
        {
            _geometryList.push_back(mesh->getPartition(p)->getIdentifier());
        }
    }
}

void Material::bindMesh(const mx::GeometryHandler& handler)
{
    assignPartitionsToMaterial(handler);

    bool haveMaterialGeometry = !_geometryList.empty();
    for (auto mesh : handler.getMeshes())
    {
        if (!haveMaterialGeometry)
        {
            bindMeshStreams(mesh);
            continue;
        }
        bool matchMeshName = std::find(_geometryList.begin(), _geometryList.end(), mesh->getIdentifier()) != _geometryList.end();
        if (matchMeshName)
        {
            bindMeshStreams(mesh);
            continue;
        }
        for (size_t partIndex = 0; partIndex < mesh->getPartitionCount(); partIndex++)
        {
            mx::MeshPartitionPtr part = mesh->getPartition(partIndex);
            bool matchPartName = std::find(_geometryList.begin(), _geometryList.end(), part->getIdentifier()) != _geometryList.end();
            if (matchPartName)
            {
                bindMeshStreams(mesh);
                break;
            }
        }
    }
}

void Material::bindMeshStreams(const mx::MeshPtr mesh) const
{
    if (!mesh || !_glShader)
    {
        return;
    }

    _glShader->bind();
    if (_glShader->attrib("i_position") != -1)
    {
        mx::MeshStreamPtr stream = mesh->getStream(mx::MeshStream::POSITION_ATTRIBUTE, 0);
        mx::MeshFloatBuffer &buffer = stream->getData();
        MatrixXfProxy positions(&buffer[0], stream->getStride(), buffer.size() / stream->getStride());
        _glShader->uploadAttrib("i_position", positions);
    }
    if (_glShader->attrib("i_normal", false) != -1)
    {
        mx::MeshStreamPtr stream = mesh->getStream(mx::MeshStream::NORMAL_ATTRIBUTE, 0);
        mx::MeshFloatBuffer &buffer = stream->getData();
        MatrixXfProxy normals(&buffer[0], stream->getStride(), buffer.size() / stream->getStride());
        _glShader->uploadAttrib("i_normal", normals);
    }
    if (_glShader->attrib("i_tangent", false) != -1)
    {
        mx::MeshStreamPtr stream = mesh->getStream(mx::MeshStream::TANGENT_ATTRIBUTE, 0);
        mx::MeshFloatBuffer &buffer = stream->getData();
        MatrixXfProxy tangents(&buffer[0], stream->getStride(), buffer.size() / stream->getStride());
        _glShader->uploadAttrib("i_tangent", tangents);
    }
    if (_glShader->attrib("i_texcoord_0", false) != -1)
    {
        mx::MeshStreamPtr stream = mesh->getStream(mx::MeshStream::TEXCOORD_ATTRIBUTE, 0);
        mx::MeshFloatBuffer &buffer = stream->getData();
        MatrixXfProxy texcoords(&buffer[0], stream->getStride(), buffer.size() / stream->getStride());
        _glShader->uploadAttrib("i_texcoord_0", texcoords);
    }
}

void Material::bindPartition(mx::MeshPartitionPtr part) const
{
    if (!_glShader)
    {
        return;
    }

    _glShader->bind();
    MatrixXuProxy indices(&part->getIndices()[0], 3, part->getIndices().size() / 3);
    _glShader->uploadIndices(indices);
}

bool Material::bindShader()
{
    if (!_glShader)
    {
        return false;
    }

    _glShader->bind();
    return true;
}

void Material::bindViewInformation(const mx::Matrix44& world, const mx::Matrix44& view, const mx::Matrix44& proj)
{
    if (!bindShader())
    {
        return;
    }

    mx::Matrix44 viewProj = proj * view;
    mx::Matrix44 invView = view.getInverse();
    mx::Matrix44 invTransWorld = world.getInverse().getTranspose();

    // Bind view properties.
    _glShader->setUniform("u_worldMatrix", ng::Matrix4f(world.getTranspose().data()));
    _glShader->setUniform("u_viewProjectionMatrix", ng::Matrix4f(viewProj.getTranspose().data()));
    if (_glShader->uniform("u_worldInverseTransposeMatrix", false) != -1)
    {
        _glShader->setUniform("u_worldInverseTransposeMatrix", ng::Matrix4f(invTransWorld.getTranspose().data()));
    }
    if (_glShader->uniform("u_viewPosition", false) != -1)
    {
        mx::Vector3 viewPosition(invView[0][3], invView[1][3], invView[2][3]);
        _glShader->setUniform("u_viewPosition", ng::Vector3f(viewPosition.data()));
    }
}

bool Material::bindImage(const std::string& filename, const std::string& uniformName, 
                         mx::GLTextureHandlerPtr imageHandler, mx::ImageDesc& desc)
{
    if (!bindShader())
    {
        return false;
    }

    // Acquire the given image.
    std::array<float, 4> defaultColor{0, 0, 0, 1};
    if (!imageHandler->acquireImage(filename, desc, true, &defaultColor))
    {
        std::cerr << "Failed to load image: " << filename << std::endl;
        return false;
    }

    // Bind the image and set its sampling properties.
    _glShader->setUniform(uniformName, desc.resourceId);
    mx::ImageSamplingProperties samplingProperties;
    imageHandler->bindImage(filename, samplingProperties);

    return true;
}

void Material::bindImages(mx::GLTextureHandlerPtr imageHandler, const mx::FileSearchPath& searchPath)
{
    if (!bindShader())
    {
        return;
    }

    const MaterialX::Shader::VariableBlock publicUniforms = _hwShader->getUniformBlock(MaterialX::Shader::PIXEL_STAGE, MaterialX::Shader::PUBLIC_UNIFORMS);
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
    if (!bindShader())
    {
        return;
    }

    // Bind light properties.
    if (_glShader->uniform("u_envSamples", false) != -1)
    {
        _glShader->setUniform("u_envSamples", envSamples);
    }
    mx::StringMap lightTextures = {
        { "u_envRadiance", "documents/TestSuite/Images/san_giuseppe_bridge.hdr" },
        { "u_envIrradiance", "documents/TestSuite/Images/san_giuseppe_bridge_diffuse.hdr" }
    };
    for (auto pair : lightTextures)
    {
        if (_glShader->uniform(pair.first, false) != -1)
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
                    _glShader->setUniform("u_envRadianceMips", desc.mipCount);
                }
            }
        }
    }
}

void Material::draw(const mx::GeometryHandler& handler) const
{
    bool nothingToMatch = _geometryList.empty();
    for (auto mesh : handler.getMeshes())
    {
        bool meshMatches = nothingToMatch || (std::find(_geometryList.begin(), _geometryList.end(), mesh->getIdentifier()) != _geometryList.end());
        for (size_t partIndex = 0; partIndex < mesh->getPartitionCount(); partIndex++)
        {
            mx::MeshPartitionPtr part = mesh->getPartition(partIndex);
            bool partMatches = meshMatches || (std::find(_geometryList.begin(), _geometryList.end(), part->getIdentifier()) != _geometryList.end());
            if (partMatches)
            {
                bindPartition(part);
                _glShader->drawIndexed(GL_TRIANGLES, 0, (uint32_t)part->getFaceCount());
            }
        }
    }
}

const MaterialX::Shader::VariableBlock* Material::getPublicUniforms() const
{
    if (!_hwShader)
    {
        return nullptr;
    }

    return &_hwShader->getUniformBlock(MaterialX::Shader::PIXEL_STAGE, MaterialX::Shader::PUBLIC_UNIFORMS);
}

mx::Shader::Variable* Material::findUniform(const std::string& path) const
{
    const MaterialX::Shader::VariableBlock* publicUniforms = getPublicUniforms();
    if (!publicUniforms)
    {
        return nullptr;
    }

    for (auto uniform : publicUniforms->variableOrder)
    {
        if (uniform->path == path)
        {
            return uniform;
        }
    }

    return nullptr;
}
