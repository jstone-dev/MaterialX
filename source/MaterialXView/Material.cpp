#include <MaterialXView/Material.h>

#include <MaterialXGenGlsl/GlslShaderGenerator.h>
#include <MaterialXGenShader/DefaultColorManagementSystem.h>
#include <MaterialXGenShader/HwShader.h>
#include <MaterialXGenShader/Util.h>

#include <iostream>

using MatrixXfProxy = Eigen::Map<const ng::MatrixXf>;
using MatrixXuProxy = Eigen::Map<const ng::MatrixXu>;

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
    options.hwSpecularEnvironmentMethod = mx::SPECULAR_ENVIRONMENT_FIS;
    options.hwTransparency = isTransparentSurface(elem, *shaderGenerator);
    options.targetColorSpaceOverride = "lin_rec709";
    mx::ShaderPtr sgShader = shaderGenerator->generate("Shader", elem, options);

    return std::dynamic_pointer_cast<mx::HwShader>(sgShader);
}

bool stringEndsWith(const std::string& str, std::string const& end)
{
    if (str.length() >= end.length())
    {
        return !str.compare(str.length() - end.length(), end.length(), end);
    }
    else
    {
        return false;
    }
}

//
// Material methods
//
mx::DocumentPtr Material::loadDocument(const mx::FilePath& filePath, mx::DocumentPtr libraries, std::vector<MaterialPtr>& materials,
                            const DocumentModifiers& modifiers)
{
    // Load the content document.
    mx::DocumentPtr doc = mx::createDocument();
    mx::XmlReadOptions readOptions;
    readOptions.readXIncludeFunction = [](mx::DocumentPtr doc, const std::string& filename, const std::string& searchPath, const mx::XmlReadOptions* options)
    {
        mx::FilePath resolvedFilename = mx::FileSearchPath(searchPath).find(filename);
        if (resolvedFilename.exists())
        {
            readFromXmlFile(doc, resolvedFilename, mx::EMPTY_STRING, options);
        }
        else
        {
            std::cerr << "Include file not found: " << filename << std::endl;
        }
    };
    mx::readFromXmlFile(doc, filePath, mx::EMPTY_STRING, &readOptions);

    // Apply modifiers to the content document if requested.
    for (mx::ElementPtr elem : doc->traverseTree())
    {
        if (modifiers.remapElements.count(elem->getCategory()))
        {
            elem->setCategory(modifiers.remapElements.at(elem->getCategory()));
        }
        if (modifiers.remapElements.count(elem->getName()))
        {
            elem->setName(modifiers.remapElements.at(elem->getName()));
        }
        mx::StringVec attrNames = elem->getAttributeNames();
        for (const std::string& attrName : attrNames)
        {
            if (modifiers.remapElements.count(elem->getAttribute(attrName)))
            {
                elem->setAttribute(attrName, modifiers.remapElements.at(elem->getAttribute(attrName)));
            }
        }
        if (elem->hasFilePrefix() && !modifiers.filePrefixTerminator.empty())
        {
            std::string filePrefix = elem->getFilePrefix();
            if (!stringEndsWith(filePrefix, modifiers.filePrefixTerminator))
            {
                elem->setFilePrefix(filePrefix + modifiers.filePrefixTerminator);
            }
        }
        std::vector<mx::ElementPtr> children = elem->getChildren();
        for (mx::ElementPtr child : children)
        {
            if (modifiers.skipElements.count(child->getCategory()) ||
                modifiers.skipElements.count(child->getName()))
            {
                elem->removeChild(child->getName());
            }
        }
    }

   // Import the given libraries.
    doc->setSourceUri(filePath);
    mx::CopyOptions copyOptions;
    copyOptions.skipDuplicateElements = true;
    doc->importLibrary(libraries, &copyOptions);

    // Remap references to unimplemented shader nodedefs.
    for (mx::MaterialPtr material : doc->getMaterials())
    {
        for (mx::ShaderRefPtr shaderRef : material->getShaderRefs())
        {
            mx::NodeDefPtr nodeDef = shaderRef->getNodeDef();
            if (nodeDef && !nodeDef->getImplementation())
            {
                std::vector<mx::NodeDefPtr> altNodeDefs = doc->getMatchingNodeDefs(nodeDef->getNodeString());
                for (mx::NodeDefPtr altNodeDef : altNodeDefs)
                {
                    if (altNodeDef->getImplementation())
                    {
                        shaderRef->setNodeDefString(altNodeDef->getName());
                    }
                }
            }
        }
    }

    // Generate materials
    std::vector<mx::TypedElementPtr> elems;
    mx::findRenderableElements(doc, elems);
    mx::ValuePtr udimSetValue = doc->getGeomAttrValue("udimset");
    for (mx::TypedElementPtr elem : elems)
    {
        if (udimSetValue && udimSetValue->isA<mx::StringVec>())
        {
            for (const std::string& udim : udimSetValue->asA<mx::StringVec>())
            {
                MaterialPtr mat = Material::create();
                mat->setElement(elem);
                mat->setUdim(udim);
                materials.push_back(mat);
            }
        }
        else
        {
            MaterialPtr mat = Material::create();
            mat->setElement(elem);
            materials.push_back(mat);
        }
    }

    return doc;
}

bool Material::generateShader(const mx::FileSearchPath& searchPath)
{
    if (!_elem)
    {
        return false;
    }
    _hwShader = generateSource(searchPath, _elem);
    if (!_hwShader)
    {
        return false;
    }

    std::string vertexShader = _hwShader->getSourceCode(mx::HwShader::VERTEX_STAGE);
    std::string pixelShader = _hwShader->getSourceCode(mx::HwShader::PIXEL_STAGE);

    _glShader = std::make_shared<ng::GLShader>();
    _glShader->init(_elem->getNamePath(), vertexShader, pixelShader);

    _hasTransparency = _hwShader->hasTransparency();

    return true;
}

void Material::bindMesh(const mx::MeshPtr mesh) const
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

bool Material::bindPartition(mx::MeshPartitionPtr part) const
{
    if (!_glShader)
    {
        return false;
    }

    _glShader->bind();
    MatrixXuProxy indices(&part->getIndices()[0], 3, part->getIndices().size() / 3);
    _glShader->uploadIndices(indices);
    
    return true;
}

void Material::bindViewInformation(const mx::Matrix44& world, const mx::Matrix44& view, const mx::Matrix44& proj)
{
    if (!_glShader)
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

void Material::bindImages(mx::GLTextureHandlerPtr imageHandler, const mx::FileSearchPath& searchPath, const std::string& udim)
{
    if (!_glShader)
    {
        return;
    }

    const MaterialX::Shader::VariableBlock publicUniforms = _hwShader->getUniformBlock(MaterialX::Shader::PIXEL_STAGE,
        MaterialX::Shader::PUBLIC_UNIFORMS);
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
        bindImage(filename, uniformName, imageHandler, desc, udim);
    }
}

bool Material::bindImage(std::string filename, const std::string& uniformName, mx::GLTextureHandlerPtr imageHandler,
    mx::ImageDesc& desc, const std::string& udim)
{
    if (!_glShader)
    {
        return false;
    }

    // Apply udim string if specified.
    if (!udim.empty())
    {
        mx::StringMap map;
        map[mx::UDIM_TOKEN] = udim;
        filename = mx::replaceSubstrings(filename, map);
    }

    // Acquire the given image.
    if (!imageHandler->acquireImage(filename, desc, true, nullptr))
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

void Material::bindLights(mx::GLTextureHandlerPtr imageHandler, const mx::FileSearchPath& imagePath, int envSamples)
{
    if (!_glShader)
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

void Material::drawPartition(mx::MeshPartitionPtr part) const
{
    if (!bindPartition(part))
    {
        return;
    }
    _glShader->drawIndexed(GL_TRIANGLES, 0, (uint32_t) part->getFaceCount());
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
