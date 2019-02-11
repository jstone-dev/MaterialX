#include <MaterialXView/Material.h>

#include <MaterialXGenShader/HwShader.h>
#include <MaterialXGenShader/Util.h>

#include <iostream>

using MatrixXfProxy = Eigen::Map<const ng::MatrixXf>;
using MatrixXuProxy = Eigen::Map<const ng::MatrixXu>;

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
size_t Material::loadDocument(mx::DocumentPtr destinationDoc, const mx::FilePath& filePath, 
                            mx::DocumentPtr libraries, const DocumentModifiers& modifiers,
                            std::vector<MaterialPtr>& materials)
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


    // Find new renderable elements
    size_t previousMaterialCount = materials.size();
    mx::StringVec renderablePaths;
    std::vector<mx::TypedElementPtr> elems;
    mx::findRenderableElements(doc, elems);
    for (mx::TypedElementPtr elem : elems)
    {
        std::string namePath = elem->getNamePath();
        // Skip adding if renderable already exists
        if (!destinationDoc->getDescendant(namePath))
        {
            renderablePaths.push_back(namePath);
        }
    }

    // Check for any udim set
    mx::ValuePtr udimSetValue = doc->getGeomAttrValue("udimset");

    // Merge the material document into the document passed in
    destinationDoc->importLibrary(doc, &copyOptions);

    // Create new materials
    for (auto renderablePath : renderablePaths)
    {
        auto elem = destinationDoc->getDescendant(renderablePath)->asA<mx::TypedElement>();
        if (!elem)
        {
            continue;
        }
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

    return (materials.size() - previousMaterialCount);
}

bool Material::generateConstantShader(const std::string& shaderName, const mx::Color4 &color)
{
    const std::string pixelShaderShaderTemplate = {
        "#version 400\n " \
        "out vec4 out1;\n" \
        "void main()\n" \
        "{\n" \
        "    out1 = vec4(_r_, _g_, _b_, _a_);\n" \
        "}\n" };
    mx::StringMap map;
    map["_r_"] = std::to_string(color[0]);
    map["_g_"] = std::to_string(color[1]);
    map["_b_"] = std::to_string(color[2]);
    map["_a_"] = std::to_string(color[3]);
    std::string pixelShader = mx::replaceSubstrings(pixelShaderShaderTemplate, map);

    const std::string vertexShader = {
        "#version 400\n" \
        "uniform mat4 u_worldMatrix = mat4(1.0);\n" \
        "uniform mat4 u_viewProjectionMatrix = mat4(1.0);\n" \
        "in vec3 i_position;\n" \
        "void main()\n" \
        "{\n" \
        "    vec4 hPositionWorld = u_worldMatrix * vec4(i_position, 1.0);\n" \
        "    gl_Position = u_viewProjectionMatrix * hPositionWorld;\n" \
        "}\n"};

    _hwShader = nullptr;
    _elem = nullptr;
    _hasTransparency = false;
    _glShader = std::make_shared<ng::GLShader>();
    return _glShader->init(shaderName, vertexShader, pixelShader);
}

mx::HwShaderPtr Material::generateSource(mx::ShaderGeneratorPtr shaderGenerator, mx::ElementPtr elem)
{
    if (!elem)
    {
        return nullptr;
    }

    mx::GenOptions options;
    options.hwSpecularEnvironmentMethod = mx::SPECULAR_ENVIRONMENT_FIS;
    options.hwTransparency = isTransparentSurface(elem, *shaderGenerator);
    options.targetColorSpaceOverride = "lin_rec709";
    mx::ShaderPtr sgShader = shaderGenerator->generate("Shader", elem, options);

    return std::dynamic_pointer_cast<mx::HwShader>(sgShader);
}

bool Material::generateShader(mx::ShaderGeneratorPtr shaderGenerator)
{
    if (!_elem)
    {
        return false;
    }
    if (!_hwShader)
    {
        _hwShader = generateSource(shaderGenerator, _elem);
    }
    if (!_hwShader)
    {
        return false;
    }
    _hasTransparency = _hwShader->hasTransparency();

    if (!_glShader)
    {
        std::string vertexShader = _hwShader->getSourceCode(mx::HwShader::VERTEX_STAGE);
        std::string pixelShader = _hwShader->getSourceCode(mx::HwShader::PIXEL_STAGE);

        _glShader = std::make_shared<ng::GLShader>();
        _glShader->init(_elem->getNamePath(), vertexShader, pixelShader);
    }
    return true;
}

void Material::bindShader(mx::ShaderGeneratorPtr shaderGenerator)
{
    generateShader(shaderGenerator);
    if (_glShader)
    {
        _glShader->bind();
    }
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
        bindImage(filename, uniformName, imageHandler, desc, udim, nullptr);
    }
}

bool Material::bindImage(std::string filename, const std::string& uniformName, mx::GLTextureHandlerPtr imageHandler,
                         mx::ImageDesc& desc, const std::string& udim, std::array<float, 4> *fallbackColor)
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
    if (!imageHandler->acquireImage(filename, desc, true, fallbackColor))
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

void Material::bindUniform(const std::string& name, const mx::Value& value)
{
    if (value.getValueString().empty())
        return;

    if (value.getTypeString() == "float")
    {
        float v = value.asA<float>();
        _glShader->setUniform(name, v);
    }
    else if (value.getTypeString() == "integer")
    {
        int v = value.asA<int>();
        _glShader->setUniform(name, v);
    }
    else if (value.getTypeString() == "boolean")
    {
        bool v = value.asA<bool>();
        _glShader->setUniform(name, v);
    }
    else if (value.getTypeString() == "color2")
    {
        mx::Color2 v = value.asA<mx::Color2>();
        ng::Vector2f ngv;
        ngv[0] = v[0];
        ngv[1] = v[1];
        _glShader->setUniform(name, ngv);
    }
    else if (value.getTypeString() == "color3")
    {
        mx::Color3 v = value.asA<mx::Color3>();
        ng::Vector3f ngv;
        ngv[0] = v[0];
        ngv[1] = v[1];
        ngv[2] = v[2];
        _glShader->setUniform(name, ngv);
    }
    else if (value.getTypeString() == "color4")
    {
        mx::Color4 v = value.asA<mx::Color4>();
        ng::Vector4f ngv;
        ngv[0] = v[0];
        ngv[1] = v[1];
        ngv[2] = v[2];
        _glShader->setUniform(name, ngv);
    }
    else if (value.getTypeString() == "vector2")
    {
        mx::Vector2 v = value.asA<mx::Vector2>();
        ng::Vector2f ngv;
        ngv[0] = v[0];
        ngv[1] = v[1];
        _glShader->setUniform(name, ngv);
    }
    else if (value.getTypeString() == "vector3")
    {
        mx::Vector3 v = value.asA<mx::Vector3>();
        ng::Vector3f ngv;
        ngv[0] = v[0];
        ngv[1] = v[1];
        ngv[2] = v[2];
        _glShader->setUniform(name, ngv);
    }
    else if (value.getTypeString() == "vector4")
    {
        mx::Vector4 v = value.asA<mx::Vector4>();
        ng::Vector4f ngv;
        ngv[0] = v[0];
        ngv[1] = v[1];
        ngv[2] = v[2];
        ngv[3] = v[3];
        _glShader->setUniform(name, ngv);
    }
}

void Material::bindLights(mx::HwLightHandlerPtr lightHandler, mx::GLTextureHandlerPtr imageHandler, 
                          const mx::FileSearchPath& imagePath, int envSamples, bool directLighting, 
                          bool indirectLighting)
{
    if (!_glShader)
    {
        return;
    }

    _glShader->bind();

    // Bind light properties.
    if (_glShader->uniform("u_envSamples", false) != -1)
    {
        _glShader->setUniform("u_envSamples", envSamples);
    }
    const std::string empty;
    mx::StringMap lightTextures = {
        { "u_envRadiance", indirectLighting ? lightHandler->getLightEnvRadiancePath() : empty },
        { "u_envIrradiance", indirectLighting ? lightHandler->getLightEnvIrradiancePath() : empty }
    };

    // On failure to load, use a black texture
    const std::string udim;
    std::array<float, 4> fallbackColor = { 0.0, 0.0, 0.0, 1.0 };
    for (auto pair : lightTextures)
    {
        if (_glShader->uniform(pair.first, false) != -1)
        {
            // Access cached image or load from disk.
            mx::FilePath path = imagePath.find(pair.second);
            const std::string filename = path.asString();

            mx::ImageDesc desc;
            if (bindImage(filename, pair.first, imageHandler, desc, udim, &fallbackColor))
            {
                // Bind any associated uniforms.
                if (pair.first == "u_envRadiance")
                {
                    _glShader->setUniform("u_envRadianceMips", desc.mipCount);
                }
            }
        }
    }

    // Check for usage of direct lighting
    if (_glShader->uniform("u_numActiveLightSources", false) == -1)
    {
        return;
    }

    // Set the number of active light sources
    unsigned int lightCount = directLighting ? static_cast<unsigned int>(lightHandler->getLightSources().size()) : 0;
    _glShader->setUniform("u_numActiveLightSources", lightCount, true);
    
    if (lightCount == 0)
    {
        return;
    }

    // Bind any direct lighting
    //
    const std::vector<mx::NodePtr> lightList = lightHandler->getLightSources();
    std::unordered_map<std::string, unsigned int> ids;
    mx::mapNodeDefToIdentiers(lightHandler->getLightSources(), ids);

    size_t index = 0;
    for (mx::NodePtr light : lightHandler->getLightSources())
    {
        auto nodeDef = light->getNodeDef();
        const std::string prefix = "u_lightData[" + std::to_string(index) + "]";

        // Set light type id
        std::string lightType(prefix + ".type");
        if (_glShader->uniform(lightType, false) != -1)
        {
            unsigned int lightTypeValue = ids[nodeDef->getName()];
            _glShader->setUniform(lightType, lightTypeValue);
        }

        // Set all inputs
        for (auto input : light->getInputs())
        {
            // Make sure we have a value to set
            if (input->hasValue())
            {
                std::string inputName(prefix + "." + input->getName());
                if (_glShader->uniform(inputName, false) != -1)
                {
                    bindUniform(inputName, *input->getValue());
                }
            }
        }

        // Set all parameters. Note that upstream node connections are not currently supported.
        for (mx::ParameterPtr param : light->getParameters())
        {
            // Make sure we have a value to set
            if (param->hasValue())
            {
                std::string paramName(prefix + "." + param->getName());
                if (_glShader->uniform(paramName, false) != -1)
                {
                    bindUniform(paramName, *param->getValue());
                }
            }
        }

        ++index;
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
