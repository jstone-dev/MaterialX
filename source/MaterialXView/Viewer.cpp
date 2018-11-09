#include <MaterialXView/Viewer.h>

#include <nanogui/button.h>
#include <nanogui/combobox.h>
#include <nanogui/layout.h>
#include <nanogui/messagedialog.h>
#include <nanogui/label.h>

#include <iostream>
#include <fstream>
#include <math.h>

const float PI = std::acos(-1.0f);

const int MIN_ENV_SAMPLES = 16;
const int MAX_ENV_SAMPLES = 256;

void writeTextFile(const std::string& text, const std::string& filePath)
{
    std::ofstream file;
    file.open(filePath);
    file << text;
    file.close();
}
  
void Viewer::updatePropertySheet()
{
    if (!_propertySheet)
    {
        _propertySheet = new ng::FormHelper(this);
    }
   
    // Bit convoluted way to clear out an existing window
    // on a FormHelper class.
    ng::Vector2i prevPosition(0, 0);
    if (_propertySheetWindow)
    {
        for (int i = 0; i < _propertySheetWindow->childCount(); i++)
        {
            _propertySheetWindow->removeChild(i);
        }
        prevPosition = _propertySheetWindow->position();
        this->removeChild(_propertySheetWindow);
    }
    _propertySheetWindow = new ng::Window(this, "Property Sheet");
    ng::AdvancedGridLayout* layout = new ng::AdvancedGridLayout({ 10, 0, 10, 0 }, {});
    layout->setMargin(10);
    layout->setColStretch(2, 1);
    _propertySheetWindow->setPosition(prevPosition);
    _propertySheetWindow->setVisible(true);
    _propertySheetWindow->setLayout(layout);
    _propertySheet->setWindow(_propertySheetWindow);

    mx::ElementPtr element = nullptr;
    if (_renderableElementIndex >= 0 && _renderableElementIndex < _renderableElements.size())
    {
        element = _renderableElements[_renderableElementIndex];
    }

    // This should be an option
    bool showOnlyBoundInputs = true;

    mx::ShaderRefPtr shaderRef = element ? element->asA<mx::ShaderRef>() : nullptr;
    if (shaderRef)
    {
        mx::NodeDefPtr nodeDef = shaderRef->getNodeDef();
        if (nodeDef)
        {
            std::vector<mx::ValuePtr> formValues;
            std::vector<mx::string> formNames;

            for (mx::ParameterPtr elem : nodeDef->getParameters())
            {
                const std::string& elemName = elem->getName();
                mx::BindParamPtr bindParam = shaderRef->getBindParam(elemName);
                mx::ValuePtr value = nullptr;
                if (bindParam)
                {
                    if (!bindParam->getValueString().empty())
                    {
                         value = bindParam->getValue();
                    }
                }
                if (!value && !showOnlyBoundInputs)
                {
                    value = elem->getValue();
                }
                if (value)
                {
                    formValues.push_back(value);
                    formNames.push_back(elemName);
                }
            }

            size_t startOfInputs = formValues.size();
            for (const mx::InputPtr& input : nodeDef->getInputs())
            {
                const std::string& elemName = input->getName();
                mx::BindInputPtr bindInput = shaderRef->getBindInput(elemName);
                mx::ValuePtr value = nullptr;
                if (bindInput)
                {
                    if (!bindInput->getValueString().empty())
                    {
                        value = bindInput->getValue();
                    }
                }
                if (!value && !showOnlyBoundInputs)
                {
                    value = input->getValue();
                }
                if (value)
                {
                    formValues.push_back(value);
                    formNames.push_back(elemName);
                }
            }

            if (formValues.size() && startOfInputs > 0)
            {
                _propertySheet->addGroup("Shader Parameters");
            }
            for (size_t i=0; i<formValues.size(); i++)
            {
                if (i == startOfInputs)
                {
                    _propertySheet->addGroup("Shader Inputs");
                }

                mx::ValuePtr value = formValues[i];
                const std::string& name = formNames[i];
                if (value->isA<int>())
                {
                    int v = value->asA<int>();
                    _propertySheet->addVariable(name, v, false);
                }
                else if (value->isA<float>())
                {
                    float v = value->asA<float>();
                    _propertySheet->addVariable(name, v, false);
                }
                else if (value->isA<mx::Color3>())
                {
                    mx::Color3 v = value->asA<mx::Color3>();
                    ng::Color c;
                    c.r() = v[0];
                    c.g() = v[1];
                    c.b() = v[2];
                    _propertySheet->addVariable(name, c, false);
                }
                else if (value->isA<mx::Color4>())
                {
                    mx::Color4 v = value->asA<mx::Color4>();
                    ng::Color c;
                    c.r() = v[0];
                    c.g() = v[1];
                    c.b() = v[2];
                    _propertySheet->addVariable(name, c, false);
                }
            }
        }
    }
    performLayout();
}

Viewer::Viewer() :
    ng::Screen(ng::Vector2i(1280, 960), "MaterialXView"),
    _translationActive(false),
    _translationStart(0, 0),
    _envSamples(MIN_ENV_SAMPLES)
{
    _window = new ng::Window(this, "Viewer Options");
    _window->setPosition(ng::Vector2i(15, 15));
    _window->setLayout(new ng::GroupLayout());

    ng::Button* meshButton = new ng::Button(_window, "Load Mesh");
    meshButton->setCallback([this]()
    {
        std::string filename = ng::file_dialog({{"obj", "Wavefront OBJ"}}, false);
        if (!filename.empty())
        {
            mProcessEvents = false;
            _mesh = MeshPtr(new Mesh());
            if (_mesh->loadMesh(filename))
            {
                if (_material)
                {
                    _material->bindMesh(_mesh);
                }
                recenterCamera();
            }
            else
            {
                new ng::MessageDialog(this, ng::MessageDialog::Type::Warning, "Mesh Loading Error", filename);
                _mesh = nullptr;
            }
            mProcessEvents = true;
        }
    });

    ng::Button* materialButton = new ng::Button(_window, "Load Material");
    materialButton->setCallback([this]()
    {
        std::string filename = ng::file_dialog({{"mtlx", "MaterialX"}}, false);
        if (!filename.empty())
        {
            mProcessEvents = false;
            _materialFilename = filename;
            try
            {
                loadDocument(_materialFilename, _materialDocument, _stdLib, _renderableElements);
                _renderableElementIndex = _renderableElements.size() ? 0 : -1;
                setElementToRender(_renderableElementIndex);
                updateMaterialComboBox();
                updatePropertySheet();
            }
            catch (std::exception& e)
            {
                new ng::MessageDialog(this, ng::MessageDialog::Type::Warning, "Shader Generation Error", e.what());
            }
            mProcessEvents = true;
        }
    });

    _materialComboBox = new ng::ComboBox(_window, {"None"});
    _materialComboBox->setCallback([this](int choice) {
        mx::ElementPtr elem = choice >= 0 ? _renderableElements[choice] : nullptr;
        _material = Material::generateShader(_searchPath, elem);
        if (_material)
        {
            _material->bindMesh(_mesh);
            _renderableElementIndex = choice;
        }
        updatePropertySheet();
    });

    mx::StringVec sampleOptions;
    for (int i = MIN_ENV_SAMPLES; i <= MAX_ENV_SAMPLES; i *= 2)
    {
        mProcessEvents = false;
        sampleOptions.push_back("Samples " + std::to_string(i));
        mProcessEvents = true;
    }
    ng::ComboBox* sampleBox = new ng::ComboBox(_window, sampleOptions);
    sampleBox->setChevronIcon(-1);
    sampleBox->setCallback([this](int index)
    {
        _envSamples = MIN_ENV_SAMPLES * (int) std::pow(2, index);
    });


    _stdLib = mx::createDocument();
    _startPath = mx::FilePath::getCurrentPath();
    _searchPath = _startPath / mx::FilePath("documents/Libraries");
    _materialFilename = std::string("documents/TestSuite/sxpbrlib/materials/standard_surface_marble_1.mtlx");

    _imageHandler = mx::TinyEXRImageHandler::create();
    loadLibraries({"stdlib", "sxpbrlib"}, _searchPath, _stdLib);

    _mesh = MeshPtr(new Mesh());
    _mesh->loadMesh("documents/TestSuite/Geometry/teapot.obj");
    recenterCamera();

    try
    {
        loadDocument(_materialFilename, _materialDocument, _stdLib, _renderableElements);
        _renderableElementIndex = _renderableElements.size() ? 0 : -1;
        setElementToRender(_renderableElementIndex);
        updateMaterialComboBox();
    }
    catch (std::exception& e)
    {
        new ng::MessageDialog(this, ng::MessageDialog::Type::Warning, "Shader Generation Error", e.what());
    }

    _propertySheet = nullptr;
    _propertySheetWindow = nullptr;
    updatePropertySheet();

    performLayout();
}

void Viewer::updateMaterialComboBox()
{
    std::vector<std::string> items;
    for (size_t i = 0; i < _renderableElements.size(); i++)
    {
        items.push_back(_renderableElements[i]->getNamePath());
    }
    _materialComboBox->setItems(items);
    performLayout();
}

bool Viewer::setElementToRender(int index)
{
    mx::ElementPtr elem = (index >= 0 && index < _renderableElements.size()) ? _renderableElements[index] : nullptr;
    if (elem)
    {
        _material = Material::generateShader(_searchPath, elem);
        if (_material)
        {
            _material->bindMesh(_mesh);
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
            loadDocument(_materialFilename, _materialDocument, _stdLib, _renderableElements);
            _renderableElementIndex = _renderableElements.size() ? 0 : -1;
            setElementToRender(_renderableElementIndex);
            updateMaterialComboBox();
            updatePropertySheet();
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
                mx::ElementPtr elem = _renderableElements.size() ? _renderableElements[0] : nullptr;
                if (elem)
                {
                    mx::HwShaderPtr hwShader = nullptr;
                    StringPair source = generateSource(_searchPath, hwShader, elem);
                    std::string baseName = mx::splitString(_materialFilename.getBaseName(), ".")[0];
                    mx::StringVec splitName = mx::splitString(baseName, ".");
                    writeTextFile(source.first, _startPath / (baseName + "_vs.glsl"));
                    writeTextFile(source.second, _startPath / (baseName + "_ps.glsl"));
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
        int elementSize = static_cast<int>(_renderableElements.size());
        if (elementSize > 1)
        {
            int newIndex = 0;
            if (key == GLFW_KEY_RIGHT)
            {
                newIndex = (_renderableElementIndex + 1) % elementSize;
            }
            else
            {
                newIndex = (_renderableElementIndex + elementSize - 1) % elementSize;
            }
            try
            {
                if (setElementToRender(newIndex))
                {
                    _materialComboBox->setSelectedIndex(newIndex);
                    _renderableElementIndex = newIndex;
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
    if (!_mesh || !_material)
    {
        return;
    }

    mx::Matrix44 world, view, proj;
    computeCameraMatrices(world, view, proj);

    GLShaderPtr shader = _material->ngShader();
    shader->bind();

    _material->bindUniforms(_imageHandler, _startPath, _envSamples, world, view, proj);

    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_FRAMEBUFFER_SRGB);
    if (_material->hasTransparency())
    {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
    else
    {
        glDisable(GL_BLEND);
    }

    shader->drawIndexed(GL_TRIANGLES, 0, (uint32_t) _mesh->getFaceCount());

    glDisable(GL_BLEND);
    glDisable(GL_FRAMEBUFFER_SRGB);
}

bool Viewer::scrollEvent(const ng::Vector2i& p, const ng::Vector2f& rel)
{
    if (!Screen::scrollEvent(p, rel))
    {
        _cameraParams.zoom = std::max(0.1f, _cameraParams.zoom * ((rel.y() > 0) ? 1.1f : 0.9f));
    }
    return true;
}

bool Viewer::mouseMotionEvent(const ng::Vector2i& p,
                              const ng::Vector2i& rel,
                              int button,
                              int modifiers)
{
    if (!Screen::mouseMotionEvent(p, rel, button, modifiers))
    {
        if (_cameraParams.arcball.motion(p))
        {
        }
        else if (_translationActive)
        {
            mx::Matrix44 world, view, proj;
            computeCameraMatrices(world, view, proj);
            mx::Matrix44 worldView = view * world;
            float zval = ng::project(ng::Vector3f(_mesh->getSphereCenter().data()),
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
            _cameraParams.modelTranslation = _cameraParams.modelTranslationStart +
                                             mx::Vector3(delta.data(), delta.data() + delta.size());
        }
    }
    return true;
}

bool Viewer::mouseButtonEvent(const ng::Vector2i& p, int button, bool down, int modifiers)
{
    if (!Screen::mouseButtonEvent(p, button, down, modifiers))
    {
        if (button == GLFW_MOUSE_BUTTON_1 && !modifiers)
        {
            _cameraParams.arcball.button(p, down);
        }
        else if (button == GLFW_MOUSE_BUTTON_2 ||
                (button == GLFW_MOUSE_BUTTON_1 && modifiers == GLFW_MOD_SHIFT))
        {
            _cameraParams.modelTranslationStart = _cameraParams.modelTranslation;
            _translationActive = true;
            _translationStart = p;
        }
    }
    if (button == GLFW_MOUSE_BUTTON_1 && !down)
    {
        _cameraParams.arcball.button(p, false);
    }
    if (!down)
    {
        _translationActive = false;
    }
    return true;
}

void Viewer::recenterCamera()
{
    _cameraParams.arcball = ng::Arcball();
    _cameraParams.arcball.setSize(mSize);
    _cameraParams.modelZoom = 2.0f / _mesh->getSphereRadius();
    _cameraParams.modelTranslation = _mesh->getSphereCenter() * -1.0f;
}

void Viewer::computeCameraMatrices(mx::Matrix44& world,
                                   mx::Matrix44& view,
                                   mx::Matrix44& proj)
{
    float fH = std::tan(_cameraParams.viewAngle / 360.0f * PI) * _cameraParams.dnear;
    float fW = fH * (float) mSize.x() / (float) mSize.y();

    ng::Matrix4f ngView = ng::lookAt(ng::Vector3f(&_cameraParams.eye[0]),
                                     ng::Vector3f(&_cameraParams.center[0]),
                                     ng::Vector3f(&_cameraParams.up[0]));
    ngView *= _cameraParams.arcball.matrix();
    ng::Matrix4f ngProj = ng::frustum(-fW, fW, -fH, fH, _cameraParams.dnear, _cameraParams.dfar);

    view = mx::Matrix44(ngView.data(), ngView.data() + ngView.size()).getTranspose();
    proj = mx::Matrix44(ngProj.data(), ngProj.data() + ngProj.size()).getTranspose();
    world = mx::Matrix44::createScale(mx::Vector3(_cameraParams.zoom * _cameraParams.modelZoom));
    world *= mx::Matrix44::createTranslation(_cameraParams.modelTranslation).getTranspose();
}
