#include <MaterialXView/Viewer.h>

#include <MaterialXRender/Handlers/stbImageLoader.h>
#include <MaterialXRender/Handlers/TinyObjLoader.h>
#include <MaterialXGenShader/Util.h>

#include <nanogui/button.h>
#include <nanogui/combobox.h>
#include <nanogui/label.h>
#include <nanogui/layout.h>
#include <nanogui/messagedialog.h>

#include <iostream>
#include <fstream>

const float PI = std::acos(-1.0f);

const int MIN_ENV_SAMPLES = 4;
const int MAX_ENV_SAMPLES = 1024;
const int DEFAULT_ENV_SAMPLES = 16;

namespace {

mx::Matrix44 createViewMatrix(const mx::Vector3& eye,
                              const mx::Vector3& target,
                              const mx::Vector3& up)
{
    mx::Vector3 z = (target - eye).getNormalized();
    mx::Vector3 x = z.cross(up).getNormalized();
    mx::Vector3 y = x.cross(z);

    return mx::Matrix44(
         x[0],  x[1],  x[2], -x.dot(eye),
         y[0],  y[1],  y[2], -y.dot(eye),
        -z[0], -z[1], -z[2],  z.dot(eye),
         0.0f,  0.0f,  0.0f,  1.0f);
}

mx::Matrix44 createPerspectiveMatrix(float left, float right,
                                     float bottom, float top,
                                     float near, float far)
{
    return mx::Matrix44(
        (2.0f * near) / (right - left), 0.0f, (right + left) / (right - left), 0.0f,
        0.0f, (2.0f * near) / (top - bottom), (top + bottom) / (top - bottom), 0.0f,
        0.0f, 0.0f, -(far + near) / (far - near), -(2.0f * far * near) / (far - near),
        0.0f, 0.0f, -1.0f, 0.0f);
}

void writeTextFile(const std::string& text, const std::string& filePath)
{
    std::ofstream file;
    file.open(filePath);
    file << text;
    file.close();
}

} // anonymous namespace

//
// Viewer methods
//

void Viewer::initializeDocument(mx::DocumentPtr libraries)
{
    _doc = mx::createDocument();
    mx::CopyOptions copyOptions;
    copyOptions.skipDuplicateElements = true;
    _doc->importLibrary(libraries, &copyOptions);

    // Clear state
    _materials.clear();
    _selectedMaterial = 0;
    _materialAssignments.clear();
}

void Viewer::importMaterials(mx::DocumentPtr materials)
{
    mx::CopyOptions copyOptions;
    copyOptions.skipDuplicateElements = true;
    _doc->importLibrary(materials, &copyOptions);
}

void Viewer::assignMaterial(MaterialPtr material, mx::MeshPartitionPtr geometry)
{
    const mx::MeshList& meshes = _geometryHandler.getMeshes();
    if (meshes.empty())
    {
        return;
    }

    material->bindMesh(meshes[0]);

    // Assign to a given mesh
    if (geometry)
    {
        if (_materialAssignments.count(geometry))
        {
            _materialAssignments[geometry] = material;
        }
    }
    // Assign to all meshes
    else
    {
        for (auto geom : _geometryList)
        {
            _materialAssignments[geom] = material;
        }
    }
}

void Viewer::createLoadMeshInterface()
{
    ng::Button* meshButton = new ng::Button(_window, "Load Mesh");
    meshButton->setIcon(ENTYPO_ICON_FOLDER);
    meshButton->setCallback([this]()
    {
        mProcessEvents = false;
        std::string filename = ng::file_dialog({ { "obj", "Wavefront OBJ" } }, false);
        if (!filename.empty())
        {
            _geometryHandler.clearGeometry();
            if (_geometryHandler.loadGeometry(filename))
            {
                updateGeometrySelections();
                // Clear out any previous assignments
                _materialAssignments.clear();
                // Bind the currently selected material (if any) to the geometry loaded in
                MaterialPtr material = getSelectedMaterial();
                if (material)
                {
                    assignMaterial(material, nullptr);
                }
                initCamera();
            }
            else
            {
                new ng::MessageDialog(this, ng::MessageDialog::Type::Warning, "Mesh Loading Error", filename);
            }
        }
        mProcessEvents = true;
    });
}

void Viewer::createLookAssignmentInterface()
{
    ng::Button* lookButton = new ng::Button(_window, "Assign Look");
    lookButton->setIcon(ENTYPO_ICON_DOCUMENT);
    lookButton->setCallback([this]()
    {
        mProcessEvents = false;
        std::string filename = ng::file_dialog({ { "mtlx", "MaterialX" } }, false);
        if (!filename.empty())
        {
            try
            {
                // Assign any materials found to geometry
                mx::DocumentPtr lookDoc = mx::createDocument();
                mx::readFromXmlFile(lookDoc, filename);
                std::vector<mx::LookPtr> looks = lookDoc->getLooks();
                // For now only handle the first look as sets of looks are not stored
                if (!looks.empty())
                {
                    auto look = looks[0];
                    std::vector<mx::MaterialAssignPtr> assignments = look->getMaterialAssigns();
                    for (auto assignment : assignments)
                    {
                        // Find if material exists
                        std::string materialName = assignment->getMaterial();

                        MaterialPtr assignedMaterial = nullptr;
                        size_t assigneMaterialIndex = 0;
                        for (size_t i = 0; i<_materials.size(); i++)
                        {
                            MaterialPtr mat = _materials[i];
                            ;                           mx::TypedElementPtr elem = mat->getElement();
                            mx::ShaderRefPtr shaderRef = elem->asA<mx::ShaderRef>();
                            mx::MaterialPtr materialRef = shaderRef ? shaderRef->getParent()->asA<mx::Material>() : nullptr;
                            if (materialRef)
                            {
                                if (materialRef->getName() == materialName)
                                {
                                    assignedMaterial = mat;
                                    assigneMaterialIndex = i;
                                    break;
                                }
                            }
                            if (assignedMaterial)
                            {
                                break;
                            }
                        }
                        if (!assignedMaterial)
                        {
                            continue;
                        }

                        // Find if geometry to assign exists and if so assign
                        // material to it.
                        mx::StringVec geomList;
                        std::string geom = assignment->getGeom();
                        if (geom.size())
                        {
                            geomList.push_back(geom);
                        }
                        else
                        {
                            mx::CollectionPtr collection = assignment->getCollection();
                            const std::string geomListString = collection->getIncludeGeom();
                            geomList = mx::splitString(geomListString, ",");
                        }

                        for (auto geomName : geomList)
                        {
                            for (size_t p = 0; p < _geometryList.size(); p++)
                            {
                                auto partition = _geometryList[p];
                                const std::string& id = partition->getIdentifier();
                                if (geomName == id)
                                {
                                    // Mimic what is done manually by choosing the geometry 
                                    // and then choosing the material to assign to that geometry
                                    setGeometrySelection(p);
                                    setMaterialSelection(assigneMaterialIndex);
                                }
                            }
                        }

                    }
                }
            }
            catch (std::exception& e)
            {
                new ng::MessageDialog(this, ng::MessageDialog::Type::Warning, "Look Assignment Error", e.what());
            }
        }
        mProcessEvents = true;
    });
}

void Viewer::createLoadMaterialsInterface(Widget *parent, const std::string label)
{
    ng::Button* materialButton = new ng::Button(parent, label);
    materialButton->setIcon(ENTYPO_ICON_FOLDER);
    materialButton->setCallback([this]()
    {
        mProcessEvents = false;
        std::string filename = ng::file_dialog({ { "mtlx", "MaterialX" } }, false);
        if (!filename.empty())
        {
            _materialFilename = filename;
            try
            {
                if (_clearExistingMaterials)
                {
                    initializeDocument(_stdLib);
                    std::vector<MaterialPtr> newMaterials;
                    mx::DocumentPtr materialDoc = Material::loadDocument(_materialFilename, _stdLib, newMaterials, _modifiers);
                    if (newMaterials.size())
                    {
                        importMaterials(materialDoc);
                        _materials.insert(_materials.end(), newMaterials.begin(), newMaterials.end());
                        updateMaterialSelections();
                        setMaterialSelection(0);
                        assignMaterial(newMaterials[0], nullptr);
                    }
                }
                else
                {
                    // TODO: Add material append.
                }
            }
            catch (std::exception& e)
            {
                new ng::MessageDialog(this, ng::MessageDialog::Type::Warning, "Shader Generation Error", e.what());
            }
        }
        mProcessEvents = true;
    });

}

Viewer::Viewer(const mx::StringVec& libraryFolders,
               const mx::FileSearchPath& searchPath,
               const std::string meshFilename,
               const std::string materialFilename,
               const DocumentModifiers& modifiers,
               int multiSampleCount) :
    ng::Screen(ng::Vector2i(1280, 960), "MaterialXView",
        true, false,
        8, 8, 24, 8,
        multiSampleCount),
    _eye(0.0f, 0.0f, 5.0f),
    _up(0.0f, 1.0f, 0.0f),
    _zoom(1.0f),
    _viewAngle(45.0f),
    _nearDist(0.05f),
    _farDist(100.0f),
    _modelZoom(1.0f),
    _translationActive(false),
    _translationStart(0, 0),
    _libraryFolders(libraryFolders),
    _searchPath(searchPath),
    _materialFilename(materialFilename),
    _modifiers(modifiers),
    _selectedGeom(0),
    _envSamples(DEFAULT_ENV_SAMPLES)
{
    _window = new ng::Window(this, "Viewer Options");
    _window->setPosition(ng::Vector2i(15, 15));
    _window->setLayout(new ng::GroupLayout());

    createLoadMeshInterface();

    ng::PopupButton *materialsBtn = new ng::PopupButton(_window, "Materials");
    materialsBtn->setIcon(ENTYPO_ICON_FOLDER);
    ng::Popup *materialsPopup = materialsBtn->popup();
    materialsPopup->setAnchorHeight(61);
    materialsPopup->setLayout(new ng::GroupLayout());
    Widget *statePanel = new Widget(materialsPopup);
    statePanel->setLayout(new ng::BoxLayout(ng::Orientation::Horizontal, ng::Alignment::Middle, 0, 5));
    // Add material load options
    createLoadMaterialsInterface(statePanel, "Load Material(s)");

    createLookAssignmentInterface();

    ng::Button* editorButton = new ng::Button(_window, "Property Editor");
    editorButton->setFlags(ng::Button::ToggleButton);
    editorButton->setChangeCallback([this](bool state)
    { 
        _propertyEditor.setVisible(state);
        performLayout();
    });

    mx::StringVec sampleOptions;
    for (int i = MIN_ENV_SAMPLES; i <= MAX_ENV_SAMPLES; i *= 4)
    {
        mProcessEvents = false;
        sampleOptions.push_back("Samples " + std::to_string(i));
        mProcessEvents = true;
    }
    ng::ComboBox* sampleBox = new ng::ComboBox(_window, sampleOptions);
    sampleBox->setChevronIcon(-1);
    sampleBox->setSelectedIndex((int) std::log2(DEFAULT_ENV_SAMPLES / MIN_ENV_SAMPLES) / 2);
    sampleBox->setCallback([this](int index)
    {
        _envSamples = MIN_ENV_SAMPLES * (int) std::pow(4, index);
    });

    _geomLabel = new ng::Label(_window, "Select Geometry");
    _geometryListBox = new ng::ComboBox(_window, {"None"});
    _geometryListBox->setChevronIcon(-1);
    _geometryListBox->setCallback([this](int choice)
    {
        setGeometrySelection(choice);
    });

    _materialLabel = new ng::Label(_window, "Assign Material To Selected Geometry");
    _materialSelectionBox = new ng::ComboBox(_window, {"None"});
    _materialSelectionBox->setChevronIcon(-1);
    _materialSelectionBox->setCallback([this](int choice)
    {
        setMaterialSelection(choice);
        assignMaterial(_materials[_selectedMaterial], _geometryList[_selectedGeom]);
    });

    // Load in standard library and create top level document
    _stdLib = loadLibraries(_libraryFolders, _searchPath);
    initializeDocument(_stdLib);

    mx::ImageLoaderPtr stbImageLoader = mx::stbImageLoader::create();
    _imageHandler = mx::GLTextureHandler::create(stbImageLoader);

    mx::TinyObjLoaderPtr loader = mx::TinyObjLoader::create();
    _geometryHandler.addLoader(loader);
    _geometryHandler.loadGeometry(meshFilename);
    updateGeometrySelections();
    
    // Initialize camera
    initCamera();
    setResizeCallback([this](ng::Vector2i size)
    {
        _arcball.setSize(size);
    });

    try
    {
        mx::DocumentPtr materialDoc = Material::loadDocument(_materialFilename, _stdLib, _materials, modifiers);
        importMaterials(materialDoc);            
        updateMaterialSelections();
        setMaterialSelection(0);
        if (_materials.size())
        {
            assignMaterial(_materials[0], nullptr);
        }
    }
    catch (std::exception& e)
    {
        new ng::MessageDialog(this, ng::MessageDialog::Type::Warning, "Failed to load materials", e.what());
    }

    updatePropertyEditor();
    _propertyEditor.setVisible(false);
    performLayout();
}

void Viewer::updateGeometrySelections()
{
    _geometryList.clear();
    mx::MeshPtr mesh = _geometryHandler.getMeshes()[0];
    for (size_t partIndex = 0; partIndex < mesh->getPartitionCount(); partIndex++)
    {
        mx::MeshPartitionPtr part = mesh->getPartition(partIndex);
        _geometryList.push_back(part);
    }

    std::vector<std::string> items;
    for (size_t i = 0; i < _geometryList.size(); i++)
    {
        std::string geomName = _geometryList[i]->getIdentifier();
        mx::StringVec geomSplit = mx::splitString(geomName, ":");
        if (!geomSplit.empty() && !geomSplit[geomSplit.size() - 1].empty())
        {
            geomName = geomSplit[geomSplit.size() - 1];
        }

        items.push_back(geomName);
    }
    _geometryListBox->setItems(items);

    _geomLabel->setVisible(items.size() > 1);
    _geometryListBox->setVisible(items.size() > 1);
    _selectedGeom = 0;

    performLayout();
}

bool Viewer::setGeometrySelection(size_t index)
{
    if (index < _geometryList.size())
    {
        _geometryListBox->setSelectedIndex((int)index);
        _selectedGeom = index;
        return true;
    }
    return false;
}

void Viewer::updateMaterialSelections()
{
    std::vector<std::string> items;
    for (auto material : _materials)
    {
        std::string displayName = material->getElement()->getNamePath();
        if (!material->getUdim().empty())
        {
            displayName += " (" + material->getUdim() + ")";
        }
        items.push_back(displayName);
    }
    _materialSelectionBox->setItems(items);

    _materialLabel->setVisible(items.size() > 1);
    _materialSelectionBox->setVisible(items.size() > 1);

    _selectedMaterial = 0;

    performLayout();
}

MaterialPtr Viewer::setMaterialSelection(size_t index)
{
    if (index >= _materials.size())
    {
        return nullptr;
    }

    // Update UI selection and internal value
    _selectedMaterial = index;
    _materialSelectionBox->setSelectedIndex((int)index);

    // Update shader if needed
    MaterialPtr material = _materials[index];
    if (material->generateShader(_searchPath))
    {
        // Record assignment
        if (!_geometryList.empty())
        {
            assignMaterial(material, _geometryList[_selectedGeom]);
        }

        // Update property editor for material
        updatePropertyEditor();
        return material;
    }
    return nullptr;
}

void Viewer::saveActiveMaterialSource()
{
    try
    {
        MaterialPtr material = getSelectedMaterial();
        mx::TypedElementPtr elem = material ? material->getElement() : nullptr;
        if (elem)
        {
            mx::HwShaderPtr hwShader = generateSource(_searchPath, elem);
            if (hwShader)
            {
                std::string vertexShader = hwShader->getSourceCode(mx::HwShader::VERTEX_STAGE);
                std::string pixelShader = hwShader->getSourceCode(mx::HwShader::PIXEL_STAGE);
                std::string baseName = elem->getName();
                writeTextFile(vertexShader, _searchPath[0] / (baseName + "_vs.glsl"));
                writeTextFile(pixelShader, _searchPath[0] / (baseName + "_ps.glsl"));
            }
        }
    }
    catch (std::exception& e)
    {
        new ng::MessageDialog(this, ng::MessageDialog::Type::Warning, "Cannot save source for material", e.what());
    }
}

bool Viewer::keyboardEvent(int key, int scancode, int action, int modifiers)
{
    if (Screen::keyboardEvent(key, scancode, action, modifiers))
    {
        return true;
    }
    if (key == GLFW_KEY_R && action == GLFW_PRESS)
    {
        try
        {
            if (!_materialFilename.isEmpty())
            {
                initializeDocument(_stdLib);
                mx::DocumentPtr materialDoc = Material::loadDocument(_materialFilename, _stdLib, _materials, _modifiers);
                importMaterials(materialDoc);
                updateMaterialSelections();
                setMaterialSelection(0);
                if (_materials.size())
                {
                    assignMaterial(_materials[0], nullptr);
                }
            }
        }
        catch (std::exception& e)
        {
            new ng::MessageDialog(this, ng::MessageDialog::Type::Warning, "Failed to load document", e.what());
        }
        try
        {
            updateMaterialSelections();
            setMaterialSelection(_selectedMaterial);
            updatePropertyEditor();
        }
        catch (std::exception& e)
        {
            new ng::MessageDialog(this, ng::MessageDialog::Type::Warning, "Shader Generation Error", e.what());
        }
        return true;
    }

    // Save active material if any
    if (key == GLFW_KEY_S && action == GLFW_PRESS)
    {
        saveActiveMaterialSource();
        return true;
    }

    // Allow left and right keys to cycle through the renderable elements
    if ((key == GLFW_KEY_RIGHT || key == GLFW_KEY_LEFT) && action == GLFW_PRESS)
    {
        size_t materialCount = _materials.size();
        size_t materialIndex = _selectedMaterial;
        if (materialCount > 1)
        {
            if (key == GLFW_KEY_RIGHT)
            {
                _selectedMaterial = (materialIndex < materialCount - 1) ? materialIndex + 1 : 0;
            }
            else
            {
                _selectedMaterial = (materialIndex > 0) ? materialIndex - 1 : materialCount - 1;
            }
            try
            {
                setMaterialSelection(_selectedMaterial);
            }
            catch (std::exception& e)
            {
                new ng::MessageDialog(this, ng::MessageDialog::Type::Warning, "Cannot assign material.", e.what());
            }
        }
        return true;
    }

    return false;
}

void Viewer::drawContents()
{
    if (_geometryList.empty() || _materials.empty())
    {
        return;
    }

    mx::Matrix44 world, view, proj;
    computeCameraMatrices(world, view, proj);

    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_FRAMEBUFFER_SRGB);

    GLShaderPtr lastBoundShader;
    for (auto assignment : _materialAssignments)
    {
        mx::MeshPartitionPtr geom = assignment.first;
        MaterialPtr material = assignment.second;
        GLShaderPtr shader = material->getShader();
        if (shader && shader != lastBoundShader)
        {
            shader->bind();
            lastBoundShader = shader;
            if (material->hasTransparency())
            {
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            }
            else
            {
                glDisable(GL_BLEND);
            }
            material->bindViewInformation(world, view, proj);
            material->bindLights(_imageHandler, _searchPath, _envSamples);
        }
        material->bindImages(_imageHandler, _searchPath, material->getUdim());
        material->drawPartition(geom);
    }

    glDisable(GL_BLEND);
    glDisable(GL_FRAMEBUFFER_SRGB);
}

bool Viewer::scrollEvent(const ng::Vector2i& p, const ng::Vector2f& rel)
{
    if (!Screen::scrollEvent(p, rel))
    {
        _zoom = std::max(0.1f, _zoom * ((rel.y() > 0) ? 1.1f : 0.9f));
    }
    return true;
}

bool Viewer::mouseMotionEvent(const ng::Vector2i& p,
                              const ng::Vector2i& rel,
                              int button,
                              int modifiers)
{
    if (Screen::mouseMotionEvent(p, rel, button, modifiers))
    {
        return true;
    }

    if (_arcball.motion(p))
    {
        return true;
    }

    if (_translationActive)
    {
        mx::Matrix44 world, view, proj;
        computeCameraMatrices(world, view, proj);
        mx::Matrix44 worldView = view * world;

        mx::MeshPtr mesh = _geometryHandler.getMeshes()[0];
        mx::Vector3 boxMin = mesh->getMinimumBounds();
        mx::Vector3 boxMax = mesh->getMaximumBounds();
        mx::Vector3 sphereCenter = (boxMax + boxMin) / 2.0;

        float zval = ng::project(ng::Vector3f(sphereCenter.data()),
                                 ng::Matrix4f(worldView.getTranspose().data()),
                                 ng::Matrix4f(proj.getTranspose().data()),
                                 mSize).z();
        ng::Vector3f pos1 = ng::unproject(ng::Vector3f((float) p.x(),
                                                       (float) (mSize.y() - p.y()),
                                                       (float) zval),
                                          ng::Matrix4f(worldView.getTranspose().data()),
                                          ng::Matrix4f(proj.getTranspose().data()),
                                          mSize);
        ng::Vector3f pos0 = ng::unproject(ng::Vector3f((float) _translationStart.x(),
                                                       (float) (mSize.y() - _translationStart.y()),
                                                       (float) zval),
                                          ng::Matrix4f(worldView.getTranspose().data()),
                                          ng::Matrix4f(proj.getTranspose().data()),
                                          mSize);
        ng::Vector3f delta = pos1 - pos0;
        _modelTranslation = _modelTranslationStart +
                            mx::Vector3(delta.data(), delta.data() + delta.size());

        return true;
    }

    return false;
}

bool Viewer::mouseButtonEvent(const ng::Vector2i& p, int button, bool down, int modifiers)
{
    if (!Screen::mouseButtonEvent(p, button, down, modifiers))
    {
        if (button == GLFW_MOUSE_BUTTON_1 && !modifiers)
        {
            _arcball.button(p, down);
        }
        else if (button == GLFW_MOUSE_BUTTON_2 ||
                (button == GLFW_MOUSE_BUTTON_1 && modifiers == GLFW_MOD_SHIFT))
        {
            _modelTranslationStart = _modelTranslation;
            _translationActive = true;
            _translationStart = p;
        }
    }
    if (button == GLFW_MOUSE_BUTTON_1 && !down)
    {
        _arcball.button(p, false);
    }
    if (!down)
    {
        _translationActive = false;
    }
    return true;
}

void Viewer::initCamera()
{
    _arcball = ng::Arcball();
    _arcball.setSize(mSize);

    mx::MeshPtr mesh = _geometryHandler.getMeshes()[0];
    mx::Vector3 boxMin = mesh->getMinimumBounds();
    mx::Vector3 boxMax = mesh->getMaximumBounds();
    mx::Vector3 sphereCenter = (boxMax + boxMin) / 2.0;
    float sphereRadius = (sphereCenter - boxMin).getMagnitude();
    _modelZoom = 2.0f / sphereRadius;
    _modelTranslation = sphereCenter * -1.0f;
}

void Viewer::computeCameraMatrices(mx::Matrix44& world,
                                   mx::Matrix44& view,
                                   mx::Matrix44& proj)
{
    float fH = std::tan(_viewAngle / 360.0f * PI) * _nearDist;
    float fW = fH * (float) mSize.x() / (float) mSize.y();

    ng::Matrix4f ngArcball = _arcball.matrix();
    mx::Matrix44 arcball = mx::Matrix44(ngArcball.data(), ngArcball.data() + ngArcball.size()).getTranspose();

    view = createViewMatrix(_eye, _center, _up) * arcball;
    proj = createPerspectiveMatrix(-fW, fW, -fH, fH, _nearDist, _farDist);
    world = mx::Matrix44::createScale(mx::Vector3(_zoom * _modelZoom));
    world *= mx::Matrix44::createTranslation(_modelTranslation).getTranspose();
}

void Viewer::updatePropertyEditor()
{
    _propertyEditor.updateContents(this);
}
