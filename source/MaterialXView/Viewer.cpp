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

Viewer::Viewer(const mx::StringVec& libraryFolders,
               const mx::FileSearchPath& searchPath,
               const mx::StringMap& nodeRemap,
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
    _nodeRemap(nodeRemap),
    _envSamples(DEFAULT_ENV_SAMPLES),
    _geomIndex(0)
{
    _window = new ng::Window(this, "Viewer Options");
    _window->setPosition(ng::Vector2i(15, 15));
    _window->setLayout(new ng::GroupLayout());

    ng::Button* meshButton = new ng::Button(_window, "Load Mesh");
    meshButton->setIcon(ENTYPO_ICON_FOLDER);
    meshButton->setCallback([this]()
    {
        mProcessEvents = false;
        std::string filename = ng::file_dialog({{"obj", "Wavefront OBJ"}}, false);
        if (!filename.empty())
        {
            _geometryHandler.clearGeometry();
            if (_geometryHandler.loadGeometry(filename))
            {
                updateGeometrySelections();
                _materials.resize(_geomSelections.size());
                for (size_t i = 1; i < _geomSelections.size(); i++)
                {
                    _materials[i] = std::make_shared<Material>();
                    *_materials[i] = *_materials[0];
                }
                if (getCurrentMaterial())
                {
                    getCurrentMaterial()->bindMesh(_geometryHandler.getMeshes()[0]);
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

    ng::Button* materialButton = new ng::Button(_window, "Load Material");
    materialButton->setIcon(ENTYPO_ICON_FOLDER);
    materialButton->setCallback([this]()
    {
        mProcessEvents = false;
        std::string filename = ng::file_dialog({{"mtlx", "MaterialX"}}, false);
        if (!filename.empty())
        {
            _materialFilename = filename;
            try
            {
                _materials[_geomIndex] = std::make_shared<Material>();
                getCurrentMaterial()->loadDocument(_materialFilename, _stdLib, _nodeRemap);
                updateSubsetSelections();
                setSubsetSelection(0);
            }
            catch (std::exception& e)
            {
                new ng::MessageDialog(this, ng::MessageDialog::Type::Warning, "Shader Generation Error", e.what());
            }
        }
        mProcessEvents = true;
    });

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

    _geomLabel = new ng::Label(_window, "Geometry");

    _geomSelectionBox = new ng::ComboBox(_window, {"None"});
    _geomSelectionBox->setChevronIcon(-1);
    _geomSelectionBox->setCallback([this](int choice)
    {
        setGeometrySelection(choice);
    });

    _materialLabel = new ng::Label(_window, "Material");

    _subsetSelectionBox = new ng::ComboBox(_window, {"None"});
    _subsetSelectionBox->setChevronIcon(-1);
    _subsetSelectionBox->setCallback([this](int choice)
    {
        setSubsetSelection(choice);
    });

    mx::ImageLoaderPtr stbImageLoader = mx::stbImageLoader::create();
    _imageHandler = mx::GLTextureHandler::create(stbImageLoader);
    _stdLib = loadLibraries(_libraryFolders, _searchPath);

    std::string meshFilename("documents/TestSuite/Geometry/teapot.obj");
    mx::TinyObjLoaderPtr loader = mx::TinyObjLoader::create();
    _geometryHandler.addLoader(loader);
    _geometryHandler.loadGeometry(meshFilename);
    updateGeometrySelections();
    initCamera();

    setResizeCallback([this](ng::Vector2i size)
    {
        _arcball.setSize(size);
    });

    _materialFilename = std::string("documents/TestSuite/pbrlib/materials/standard_surface_default.mtlx");
    _materials.push_back(std::make_shared<Material>());

    try
    {
        getCurrentMaterial()->loadDocument(_materialFilename, _stdLib, _nodeRemap);
    }
    catch (std::exception& e)
    {
        new ng::MessageDialog(this, ng::MessageDialog::Type::Warning, "Failed to load document", e.what());
    }
    try
    {
        updateSubsetSelections();
        setSubsetSelection(0);
    }
    catch (std::exception& e)
    {
        new ng::MessageDialog(this, ng::MessageDialog::Type::Warning, "Shader Generation Error", e.what());
    }

    updatePropertyEditor();
    _propertyEditor.setVisible(false);
    performLayout();
}

void Viewer::updateGeometrySelections()
{
    _geomSelections.clear();
    mx::MeshPtr mesh = _geometryHandler.getMeshes()[0];
    for (size_t partIndex = 0; partIndex < mesh->getPartitionCount(); partIndex++)
    {
        mx::MeshPartitionPtr part = mesh->getPartition(partIndex);
        _geomSelections.push_back(part);
    }

    std::vector<std::string> items;
    for (size_t i = 0; i < _geomSelections.size(); i++)
    {
        std::string geomName = _geomSelections[i]->getIdentifier();
        mx::StringVec geomSplit = mx::splitString(geomName, ":");
        if (!geomSplit.empty() && !geomSplit[geomSplit.size() - 1].empty())
        {
            geomName = geomSplit[geomSplit.size() - 1];
        }

        items.push_back(geomName);
    }
    _geomSelectionBox->setItems(items);

    _geomLabel->setVisible(items.size() > 1);
    _geomSelectionBox->setVisible(items.size() > 1);
    _geomIndex = 0;

    performLayout();
}

bool Viewer::setGeometrySelection(size_t index)
{
    if (index < _geomSelections.size())
    {
        _geomIndex = index;
        updateSubsetSelections();
        setSubsetSelection(getCurrentMaterial()->getSubsetIndex());
        return true;
    }
    return false;
}

void Viewer::updateSubsetSelections()
{
    std::vector<std::string> items;
    for (const MaterialSubset& subset : getCurrentMaterial()->getSubsets())
    {
        std::string displayName = subset.elem->getNamePath();
        if (!subset.udim.empty())
        {
            displayName += " (" + subset.udim + ")";
        }
        items.push_back(displayName);
    }
    _subsetSelectionBox->setItems(items);

    _materialLabel->setVisible(items.size() > 1);
    _subsetSelectionBox->setVisible(items.size() > 1);

    performLayout();
}

bool Viewer::setSubsetSelection(size_t index)
{
    MaterialPtr material = getCurrentMaterial();
    if (index >= material->getSubsets().size())
    {
        return false;
    }

    getCurrentMaterial()->setSubsetIndex(index);

    const MaterialSubset& subset = material->getCurrentSubset();
    if (subset.elem)
    {
        _subsetSelectionBox->setSelectedIndex((int) index);
        if (material->generateShader(_searchPath, subset.elem))
        {
            material->bindImages(_imageHandler, _searchPath, subset.udim);
            material->bindMesh(_geometryHandler.getMeshes()[0]);
            updatePropertyEditor();
            return true;
        }
    }
    return false;
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
            getCurrentMaterial()->loadDocument(_materialFilename, _stdLib, _nodeRemap);
        }
        catch (std::exception& e)
        {
            new ng::MessageDialog(this, ng::MessageDialog::Type::Warning, "Failed to load document", e.what());
        }
        try
        {
            updateSubsetSelections();
            setSubsetSelection(getCurrentMaterial()->getSubsetIndex());
            updatePropertyEditor();
        }
        catch (std::exception& e)
        {
            new ng::MessageDialog(this, ng::MessageDialog::Type::Warning, "Shader Generation Error", e.what());
        }
        return true;
    }
    if (key == GLFW_KEY_S && action == GLFW_PRESS)
    {
        if (!_materialFilename.isEmpty())
        {
            try
            {
                MaterialPtr material = getCurrentMaterial();
                MaterialSubset subset = material->getCurrentSubset();
                if (subset.elem)
                {
                    mx::HwShaderPtr hwShader = generateSource(_searchPath, subset.elem);
                    if (hwShader)
                    {
                        std::string vertexShader = hwShader->getSourceCode(mx::HwShader::VERTEX_STAGE);
                        std::string pixelShader = hwShader->getSourceCode(mx::HwShader::PIXEL_STAGE);
                        std::string baseName = mx::splitString(_materialFilename.getBaseName(), ".")[0];
                        writeTextFile(vertexShader, _searchPath[0] / (baseName + "_vs.glsl"));
                        writeTextFile(pixelShader, _searchPath[0]  / (baseName + "_ps.glsl"));
                    }
                }
            }
            catch (std::exception& e)
            {
                new ng::MessageDialog(this, ng::MessageDialog::Type::Warning, "Shader Generation Error", e.what());
            }
        }
        return true;
    }

    // Allow left and right keys to cycle through the renderable elements
    if ((key == GLFW_KEY_RIGHT || key == GLFW_KEY_LEFT) && action == GLFW_PRESS)
    {
        size_t subsetCount = getCurrentMaterial()->getSubsets().size();
        size_t subsetIndex = getCurrentMaterial()->getSubsetIndex();
        if (subsetCount > 1)
        {
            size_t newIndex = 0;
            if (key == GLFW_KEY_RIGHT)
            {
                newIndex = (subsetIndex < subsetCount - 1) ? subsetIndex + 1 : 0;
            }
            else
            {
                newIndex = (subsetIndex > 0) ? subsetIndex - 1 : subsetCount - 1;
            }
            try
            {
                if (setSubsetSelection(newIndex))
                {
                    _subsetSelectionBox->setSelectedIndex((int) newIndex);
                    updateSubsetSelections();
                    updatePropertyEditor();
                }
            }
            catch (std::exception& e)
            {
                new ng::MessageDialog(this, ng::MessageDialog::Type::Warning, "Shader Generation Error", e.what());
            }
        }
        return true;
    }

    return false;
}

void Viewer::drawContents()
{
    if (_geomSelections.empty() || !getCurrentMaterial())
    {
        return;
    }

    mx::Matrix44 world, view, proj;
    computeCameraMatrices(world, view, proj);

    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_FRAMEBUFFER_SRGB);

    GLShaderPtr lastBoundShader;
    for (size_t i = 0; i < _geomSelections.size(); i++)
    {
        MaterialPtr material = _materials[i];
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
        material->bindImages(_imageHandler, _searchPath, material->getCurrentSubset().udim);
        material->drawPartition(_geomSelections[i]);
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
