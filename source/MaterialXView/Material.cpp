#include <MaterialXView/Material.h>

#include <MaterialXGenShader/Util.h>
#include <MaterialXGenGlsl/GlslShaderGenerator.h>
#include <MaterialXGenShader/HwShader.h>

#include <iostream>
#include <unordered_set>

using MatrixXfProxy = Eigen::Map<const ng::MatrixXf>;
using MatrixXuProxy = Eigen::Map<const ng::MatrixXu>;

std::unordered_map<std::string, ImageDesc> imageCache;

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

void loadDocument(const mx::FilePath& filePath, mx::DocumentPtr& doc, mx::DocumentPtr stdLib, std::vector<mx::ElementPtr>& elements)
{
    elements.clear();

    doc = mx::createDocument();
    mx::readFromXmlFile(doc, filePath);
    doc->importLibrary(stdLib);

    std::vector<mx::NodeGraphPtr> nodeGraphs = doc->getNodeGraphs();
    std::vector<mx::OutputPtr> outputList = doc->getOutputs();
    std::unordered_set<mx::OutputPtr> outputSet(outputList.begin(), outputList.end());
    std::vector<mx::MaterialPtr> materials = doc->getMaterials();

    if (!materials.empty() || !nodeGraphs.empty() || !outputList.empty())
    {
        std::unordered_set<mx::OutputPtr> shaderrefOutputs;
        for (auto material : materials)
        {
            for (auto shaderRef : material->getShaderRefs())
            {
                if (!shaderRef->hasSourceUri())
                {
                    // Add in all shader references which are not part of a node definition library
                    elements.push_back(shaderRef);

                    // Find all bindinputs which reference outputs and outputgraphs
                    for (auto bindInput : shaderRef->getBindInputs())
                    {
                        mx::OutputPtr outputPtr = bindInput->getConnectedOutput();
                        if (outputPtr)
                        {
                            shaderrefOutputs.insert(outputPtr);
                        }
                    }
                }
            }
        }

        // Find node graph outputs
        for (mx::NodeGraphPtr nodeGraph : nodeGraphs)
        {
            // Skip anything from an include file including libraries.
            if (!nodeGraph->hasSourceUri())
            {
                std::vector<mx::OutputPtr> nodeGraphOutputs = nodeGraph->getOutputs();
                for (mx::OutputPtr output : nodeGraphOutputs)
                {
                    // We skip any outputs which are referenced elsewhere.
                    if (shaderrefOutputs.count(output) == 0)
                    {
                        outputSet.insert(output);
                    }
                }
            }
        }

        // Add ouptuts which are not part of library definitions
        for (mx::OutputPtr output : outputSet)
        {
            // Skip anything from include files
            if (!output->hasSourceUri())
            {
                elements.push_back(output);
            }
        }
    }
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

bool Material::acquireTexture(const std::string& filename, mx::ImageHandlerPtr imageHandler, ImageDesc& desc)
{
    if (imageCache.count(filename))
    {
        desc = imageCache[filename];
    }
    else
    {
        // Load the requested texture into memory.
        float* data = nullptr;
        if (!imageHandler->loadImage(filename, desc.width, desc.height, desc.channelCount, &data))
        {
            std::cerr << "Failed to load image: " << filename << std::endl;
            return false;
        }
        desc.mipCount = (unsigned int)std::log2(std::max(desc.width, desc.height)) + 1;

        // Upload texture and generate mipmaps.
        glGenTextures(1, &desc.textureId);
        glActiveTexture(GL_TEXTURE0 + desc.textureId);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glBindTexture(GL_TEXTURE_2D, desc.textureId);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, desc.width, desc.height, 0,
            (desc.channelCount == 4 ? GL_RGBA : GL_RGB), GL_FLOAT, data);
        glGenerateMipmap(GL_TEXTURE_2D);

        // Free memory buffer.
        free(data);

        imageCache[filename] = desc;
    }
    return true;
}

bool Material::bindTexture(const std::string& filename, const std::string& uniformName, 
                           mx::ImageHandlerPtr imageHandler, ImageDesc& desc)
{
    if (!_ngShader)
    {
        return false;
    }

    _ngShader->bind();

    if (acquireTexture(filename, imageHandler, desc))
    {
        // Bind texture to shader.
        _ngShader->setUniform(uniformName, desc.textureId);

        glActiveTexture(GL_TEXTURE0 + desc.textureId);
        glBindTexture(GL_TEXTURE_2D, desc.textureId);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);

        return true;
    }
    return false;
}

void Material::bindTextures(mx::ImageHandlerPtr imageHandler, mx::FilePath imagePath)
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

        ImageDesc desc;
        bindTexture(filename, uniformName, imageHandler, desc);
    }
}

void Material::bindUniforms(mx::ImageHandlerPtr imageHandler, mx::FilePath imagePath, int envSamples,
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

            ImageDesc desc;
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
