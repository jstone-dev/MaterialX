#ifndef MATERIALXVIEW_VIEWER_H
#define MATERIALXVIEW_VIEWER_H

#include <MaterialXView/Editor.h>
#include <MaterialXView/Material.h>
#include <MaterialXRender/Handlers/GeometryHandler.h>

namespace mx = MaterialX;
namespace ng = nanogui;

class Viewer : public ng::Screen
{
  public:
    Viewer(const mx::StringVec& libraryFolders,
           const mx::FileSearchPath& searchPath,
           const mx::StringMap& nodeRemap,
           int multiSampleCount);
    ~Viewer() { }

    void drawContents() override;
    bool keyboardEvent(int key, int scancode, int action, int modifiers) override;

    bool scrollEvent(const ng::Vector2i& p, const ng::Vector2f& rel) override;
    bool mouseMotionEvent(const ng::Vector2i& p, const ng::Vector2i& rel, int button, int modifiers) override;
    bool mouseButtonEvent(const ng::Vector2i& p, int button, bool down, int modifiers) override;

    ng::Window* getWindow() const
    {
        return _window;
    }

    MaterialPtr getSelectedMaterial() const
    {
        if (_selectedMaterial < _materials.size())
        {
            return _materials[_selectedMaterial];
        }
        return nullptr;
    }

    mx::DocumentPtr getCurrentDocument() const
    {
        return _doc;
    }

    const mx::FileSearchPath& getSearchPath() const
    {
        return _searchPath;
    }

    const mx::GLTextureHandlerPtr getImageHandler() const
    {
        return _imageHandler;
    }

  private:
    void initializeDocument(mx::DocumentPtr libraries);
    void importMaterials(mx::DocumentPtr materials);

    /// Assign material ro a given geometry if given. If an empty supplied,
    /// then assign to all geometries.
    void assignMaterial(MaterialPtr material, mx::MeshPartitionPtr geometry);
    void initCamera();
    void computeCameraMatrices(mx::Matrix44& world,
                               mx::Matrix44& view,
                               mx::Matrix44& proj);

    bool setGeometrySelection(size_t index);
    void updateGeometrySelections();

    MaterialPtr setMaterialSelection(size_t index);
    void updateMaterialSelections();

    void updatePropertyEditor();

  private:
    ng::Window* _window;
    ng::Arcball _arcball;

    mx::Vector3 _eye;
    mx::Vector3 _center;
    mx::Vector3 _up;
    float _zoom;
    float _viewAngle;
    float _nearDist;
    float _farDist;

    mx::Vector3 _modelTranslation;
    mx::Vector3 _modelTranslationStart;
    float _modelZoom;

    bool _translationActive;
    ng::Vector2i _translationStart;

    // Document management
    mx::StringVec _libraryFolders;
    mx::FileSearchPath _searchPath;
    mx::StringMap _nodeRemap;
    mx::DocumentPtr _stdLib;
    mx::DocumentPtr _doc;
    mx::FilePath _materialFilename;

    // List of available materials and UI
    std::vector<MaterialPtr> _materials;
    size_t _selectedMaterial;
    // UI:
    ng::Label* _materialLabel;
    ng::ComboBox* _materialSelectionBox;
    // Property editor: Currently shows only selected material
    PropertyEditor _propertyEditor;

    // List of available geometries and UI
    std::vector<mx::MeshPartitionPtr> _geometryList;
    size_t _selectedGeom;
    // UI:
    ng::Label* _geomLabel;
    ng::ComboBox* _geometryListBox;

    // List of material assignments to geometry
    std::map<mx::MeshPartitionPtr, MaterialPtr> _materialAssignments;

    // Resource handlers
    mx::GeometryHandler _geometryHandler;
    mx::GLTextureHandlerPtr _imageHandler;

    // FIS sample count
    int _envSamples;
};

#endif // MATERIALXVIEW_VIEWER_H
