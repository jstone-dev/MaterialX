#include <MaterialXView/Material.h>

#include <MaterialXGenShader/Util.h>
#include <MaterialXGenGlsl/GlslShaderGenerator.h>
#include <MaterialXGenShader/HwShader.h>

#include <iostream>

using MatrixXfProxy = Eigen::Map<const ng::MatrixXf>;
using MatrixXuProxy = Eigen::Map<const ng::MatrixXu>;

// TODO: Move image caching into the ImageHandler class.
class ImageDesc
{
  public:
    unsigned int width = 0; // TODO: These would be better as size_t.
    unsigned int height = 0;
    unsigned int channelCount = 0;
    unsigned int mipCount = 0;
    unsigned int textureId = 0;
};

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

StringPair generateSource(const mx::FilePath& filePath, const mx::FilePath& searchPath, mx::DocumentPtr stdLib, mx::HwShaderPtr& hwShader)
{
    mx::DocumentPtr doc = mx::createDocument();
    mx::readFromXmlFile(doc, filePath);
    doc->importLibrary(stdLib);

    mx::ElementPtr elem = nullptr;
    for (mx::MaterialPtr material : doc->getMaterials())
    {
        std::vector<mx::ShaderRefPtr> shaderRefs = material->getShaderRefs();
        if (!shaderRefs.empty())
        {
            elem = shaderRefs[0];
            break;
        }
    }
    if (!elem)
    {
        for (mx::NodeGraphPtr nodeGraph : doc->getNodeGraphs())
        {
            std::vector<mx::OutputPtr> outputs = nodeGraph->getOutputs();
            if (!outputs.empty())
            {
                elem = outputs[0];
                break;
            }
        }
    }
    if (!elem)
    {
        return StringPair();
    }

    mx::ShaderGeneratorPtr shaderGenerator = mx::GlslShaderGenerator::create();
    shaderGenerator->registerSourceCodeSearchPath(searchPath);

    mx::GenOptions options;
    options.shaderInterfaceType = mx::SHADER_INTERFACE_REDUCED;
    options.hwTransparency = isTransparentSurface(elem, *shaderGenerator);
    mx::ShaderPtr sgShader = shaderGenerator->generate("Shader", elem, options);

    std::string vertexShader = sgShader->getSourceCode(mx::HwShader::VERTEX_STAGE);
    std::string pixelShader = sgShader->getSourceCode(mx::HwShader::PIXEL_STAGE);

    hwShader = std::dynamic_pointer_cast<mx::HwShader>(sgShader);

    return StringPair(vertexShader, pixelShader);
}

ViewerShaderPtr ViewerShader::generateShader(const mx::FilePath& filePath, const mx::FilePath& searchPath, mx::DocumentPtr stdLib )
{
    mx::HwShaderPtr hwShader = nullptr;
    StringPair source = generateSource(filePath, searchPath, stdLib, hwShader);
    if (!source.first.empty() && !source.second.empty())
    {
        GLShaderPtr ngShader = GLShaderPtr(new ng::GLShader());
        ngShader->init(filePath.getBaseName(), source.first, source.second);

        ViewerShaderPtr shader = ViewerShaderPtr(new ViewerShader(ngShader, hwShader));
        shader->_isTransparent = hwShader ? hwShader->hasTransparency() : false;

        return shader;
    }
    return nullptr;
}

void ViewerShader::bindMesh(MeshPtr& mesh)
{
    if (!mesh || !_ngShader)
    {
        return;
    }

    MatrixXfProxy positions(&mesh->getPositions()[0][0], 3, mesh->getPositions().size());
    MatrixXfProxy normals(&mesh->getNormals()[0][0], 3, mesh->getNormals().size());
    MatrixXfProxy tangents(&mesh->getTangents()[0][0], 3, mesh->getTangents().size());
    MatrixXfProxy texcoords(&mesh->getTexcoords()[0][0], 2, mesh->getTexcoords().size());
    MatrixXuProxy indices(&mesh->getIndices()[0], 3, mesh->getIndices().size() / 3);

    _ngShader->bind();
    _ngShader->uploadAttrib("i_position", positions);
    _ngShader->uploadAttrib("i_normal", normals);
    _ngShader->uploadAttrib("i_tangent", tangents);
    _ngShader->uploadAttrib("i_texcoord_0", texcoords);
    _ngShader->uploadIndices(indices);
}

void ViewerShader::bindUniforms(mx::ImageHandlerPtr imageHandler, mx::FilePath imagePath, int envSamples,
                  mx::Matrix44& world, mx::Matrix44& view, mx::Matrix44& proj)
{
    GLShaderPtr shader = ngShader();
    mx::HwShaderPtr hwShader = mxShader();

    int textureUnit = 0;
    const MaterialX::Shader::VariableBlock publicUniforms = hwShader->getUniformBlock(MaterialX::Shader::PIXEL_STAGE, MaterialX::Shader::PUBLIC_UNIFORMS);
    for (auto uniform : publicUniforms.variableOrder)
    {
        if (uniform->type != MaterialX::Type::FILENAME)
        {
            continue;
        }
        const std::string& samplerName = uniform->name;
        const std::string& fileName = uniform->value ? uniform->value->getValueString() : "";
        const std::string kTEXTUREPOSTFIX("_texture");
        std::string textureName = samplerName + kTEXTUREPOSTFIX;

        ImageDesc desc;
        if (imageCache.count(fileName))
        {
            desc = imageCache[fileName];
        }
        else
        {
            // Load the requested texture into memory.
            float* data = nullptr;
            if (!imageHandler->loadImage(fileName, desc.width, desc.height, desc.channelCount, &data))
            {
                std::cerr << "Failed to load image: " << fileName << std::endl;
                continue;
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

            imageCache[fileName] = desc;
        }

        // Bind texture to shader.
        shader->setUniform(uniform->name, desc.textureId);
        glActiveTexture(GL_TEXTURE0 + desc.textureId);
        glBindTexture(GL_TEXTURE_2D, desc.textureId);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);

        textureUnit++;
    }

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
            ImageDesc desc;
            if (imageCache.count(path))
            {
                desc = imageCache[path];
            }
            else
            {
                // Load the requested texture into memory.
                float* data = nullptr;
                if (!imageHandler->loadImage(path, desc.width, desc.height, desc.channelCount, &data))
                {
                    std::cerr << "Failed to load image: " << path.asString() << std::endl;
                    continue;
                }
                desc.mipCount = (unsigned int) std::log2(std::max(desc.width, desc.height)) + 1;

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

                imageCache[path] = desc;
            }

            // Bind texture to shader.
            shader->setUniform(pair.first, desc.textureId);
            glActiveTexture(GL_TEXTURE0 + desc.textureId);
            glBindTexture(GL_TEXTURE_2D, desc.textureId);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);

            // Bind any associated uniforms.
            if (pair.first == "u_envRadiance")
            {
                shader->setUniform("u_envRadianceMips", desc.mipCount);
            }

            textureUnit++;
        }
    }
}
