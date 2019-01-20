#ifndef MATERIALXVIEW_MATERIAL_H
#define MATERIALXVIEW_MATERIAL_H

#include <MaterialXRender/Handlers/GeometryHandler.h>

#include <MaterialXCore/Document.h>
#include <MaterialXFormat/XmlIo.h>
#include <MaterialXFormat/File.h>

#include <MaterialXRender/OpenGL/GLTextureHandler.h>
#include <MaterialXGenShader/HwShader.h>

#include <nanogui/common.h>
#include <nanogui/glutil.h>

namespace mx = MaterialX;
namespace ng = nanogui;

using MaterialPtr = std::shared_ptr<class Material>;
using GLShaderPtr = std::shared_ptr<ng::GLShader>;

using StringPair = std::pair<std::string, std::string>;

class Material
{
  public:
    static MaterialPtr generateMaterial(const mx::FileSearchPath& searchPath, mx::ElementPtr elem);

    /// Get the NanoGUI shader associated with this material.
    GLShaderPtr getShader() const { return _ngShader; }
    
    /// Bind mesh given a handler.
    void bindMesh(const mx::GeometryHandler& handler);

    /// Bind viewing information to shader.
    void bindViewInformation(const mx::Matrix44& world, const mx::Matrix44& view, const mx::Matrix44& proj);

    /// Bind image to shader.
    bool bindImage(const std::string& filename, const std::string& uniformName,
                   mx::GLTextureHandlerPtr imageHandler, mx::ImageDesc& desc);

    /// Bind required images to shader.
    void bindImages(mx::GLTextureHandlerPtr imageHandler, const mx::FileSearchPath& imagePath);

    /// Bind lights to shader.
    void bindLights(mx::GLTextureHandlerPtr imageHandler, const mx::FileSearchPath& imagePath, int envSamples);

    /// Return if the shader is has transparency.
    bool hasTransparency() const { return _hasTransparency; }

    /// Draw the mesh given a handler.
    void draw(const mx::GeometryHandler& handler) const;

    /// List of associated geometry for this material.
    mx::StringVec& getGeometryList()
    {
        return _geometryList;
    }

    // Return the block of public uniforms for this material.
    const mx::Shader::VariableBlock* Material::getPublicUniforms() const;

    /// Find a public uniform from its MaterialX path.
    mx::Shader::Variable* findUniform(const std::string& path) const;

  protected:
    Material(GLShaderPtr ngshader, mx::HwShaderPtr mxshader) :
        _ngShader(ngshader),
        _mxShader(mxshader),
        _hasTransparency(mxshader ? mxshader->hasTransparency() : false)
    {
    }

    // Utility to set associated geometry for the material. For now it
    // assigns all partitions to the the material
    void assignPartitionsToMaterial(const mx::GeometryHandler handler);

    /// Bind mesh streams given a mesh
    void bindMeshStreams(mx::MeshPtr mesh) const;

    /// Bind mesh partition 
    void bindPartition(mx::MeshPartitionPtr part) const;

    GLShaderPtr _ngShader;
    mx::HwShaderPtr _mxShader;
    bool _hasTransparency;
    mx::StringVec _geometryList;
};

mx::DocumentPtr loadDocument(const mx::FilePath& filePath, mx::DocumentPtr stdLib);
mx::DocumentPtr loadLibraries(const mx::StringVec& libraryNames, const mx::FileSearchPath& searchPath);
void remapNodes(mx::DocumentPtr& doc, const mx::StringMap& nodeRemap);

StringPair generateSource(const mx::FileSearchPath& searchPath, mx::HwShaderPtr& hwShader, mx::ElementPtr elem);

#endif // MATERIALXVIEW_MATERIAL_H
