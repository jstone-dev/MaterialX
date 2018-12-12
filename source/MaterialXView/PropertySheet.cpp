#include <MaterialXView/Viewer.h>
#include <MaterialXView/PropertySheet.h>

#include <nanogui/button.h>
#include <nanogui/combobox.h>
#include <nanogui/label.h>
#include <nanogui/layout.h>
#include <nanogui/messagedialog.h>

#include <MaterialXGenShader/Util.h>

PropertySheet::PropertySheet()
    : _form(nullptr),
    _formWindow(nullptr),
    _visible(false) // Start up with property sheet hidden
{
}


// Local derived class helper to allow for access to override some additional
// parameters
class myFormHelper : public ng::FormHelper
{
public:
    myFormHelper(ng::Screen *screen) : ng::FormHelper(screen)
    {}

    void setPreGroupSpacing(int val)
    {
        mPreGroupSpacing = val;
    }

    void setPostGroupSpacing(int val)
    {
        mPostGroupSpacing = val;
    }

    void setVariableSpacing(int val)
    {
        mVariableSpacing = val;
    }
};

void PropertySheet::create(Viewer& parent)
{
    ng::Window* parentWindow = parent.getWindow();
    if (!_form)
    {
        myFormHelper* form = new myFormHelper(&parent);
        form->setPreGroupSpacing(2);
        form->setPostGroupSpacing(2);
        form->setVariableSpacing(2);
        _form = form;
    }

    // Remove the window associated with the form.
    // This is done by explicitly creating and owning the window
    // as opposed to having it being done by the form
    ng::Vector2i previousPosition(15, parentWindow->height() + 60);
    if (_formWindow)
    {
        for (int i = 0; i < _formWindow->childCount(); i++)
        {
            _formWindow->removeChild(i);
        }
        // We don't want the property sheet to move when
        // we update it's contents so cache any previous position
        // to use when we create a new window.
        previousPosition = _formWindow->position();
        parent.removeChild(_formWindow);
    }

    _formWindow = new ng::Window(&parent, "Property Sheet");
    ng::AdvancedGridLayout* layout = new ng::AdvancedGridLayout({ 10, 0, 10, 0 }, {});
    layout->setMargin(2);
    layout->setColStretch(2, 0);
    if (previousPosition.x() < 0)
        previousPosition.x() = 0;
    if (previousPosition.y() < 0)
        previousPosition.y() = 0;
    _formWindow->setPosition(previousPosition);
    _formWindow->setVisible(_visible);
    _formWindow->setLayout(layout);
    _form->setWindow(_formWindow);
}

void PropertySheet::addValueToForm(mx::ValuePtr value, const std::string& label,
                            const std::string& path, mx::ValuePtr min, mx::ValuePtr max,
                            const mx::StringVec& enumeration, const std::vector<mx::ValuePtr> enumValues,
                            const std::string& group, ng::FormHelper& form, Viewer* viewer)
{
    if (!value)
    {
        return;
    }

    if (!group.empty())
    {
        form.addGroup(group);
    }

    // Integer input. Can map to a combo box if an enumeration
    if (value->isA<int>())
    {
        int v = value->asA<int>();

        // Create a combo box. The items are the enumerations in order.
        if (v < enumeration.size())
        {
            std::string enumValue = enumeration[v];

            ng::ComboBox* comboBox = new ng::ComboBox(form.window(), {""});
            comboBox->setItems(enumeration);
            comboBox->setSelectedIndex(v);
            comboBox->setFontSize(form.widgetFontSize());
            form.addWidget(label, comboBox);
            comboBox->setCallback([this, path, viewer, enumValues](int v)
            {
                Material* material = viewer->getMaterial();
                mx::Shader::Variable* uniform = material ? material->findUniform(path) : nullptr;
                if (uniform)
                {
                    material->ngShader()->bind();
                    if (v < enumValues.size())
                    {
                        material->ngShader()->setUniform(uniform->name, enumValues[v]->asA<int>());
                    }
                    else
                    {
                        material->ngShader()->setUniform(uniform->name, v);
                    }
                }
            });
        }
        else
        {
            nanogui::detail::FormWidget<int, std::true_type>* intVar =
                form.addVariable(label, v, true);
            intVar->setSpinnable(true);
            intVar->setCallback([this, path, viewer](int v)
            {
                Material* material = viewer->getMaterial();
                if (material)
                {
                    mx::Shader::Variable* uniform = material->findUniform(path);
                    if (uniform)
                    {
                        material->ngShader()->bind();
                        material->ngShader()->setUniform(uniform->name, v);
                    }
                }
            });
        }
    }

    // Float widget
    else if (value->isA<float>())
    {
        float v = value->asA<float>();
        nanogui::detail::FormWidget<float, std::true_type>* floatVar =
            form.addVariable(label, v, true);
        floatVar->setSpinnable(true);
        if (min)
            floatVar->setMinValue(min->asA<float>());
        if (max)
            floatVar->setMaxValue(max->asA<float>());
        floatVar->setCallback([this, path, viewer](float v)
        {
            Material* material = viewer->getMaterial();
            if (material)
            {
                mx::Shader::Variable* uniform = material ? material->findUniform(path) : nullptr;
                if (uniform)
                {
                    material->ngShader()->bind();
                    material->ngShader()->setUniform(uniform->name, v);
                }                
            }
        });
    }

    // Color2 input
    else if (value->isA<mx::Color2>())
    {
        mx::Color2 v = value->asA<mx::Color2>();
        ng::Color c;
        c.r() = v[0];
        c.g() = v[1];
        c.b() = 0.0f;
        c.w() = 1.0f;
        nanogui::detail::FormWidget<nanogui::Color, std::true_type>* colorVar =
            form.addVariable(label, c, true);
        colorVar->setFinalCallback([this, path, viewer, colorVar](const ng::Color &c)
        {
            Material* material = viewer->getMaterial();
            mx::Shader::Variable* uniform = material ? material->findUniform(path) : nullptr;
            if (uniform)
            {
                material->ngShader()->bind();
                ng::Vector2f v;
                v.x() = c.r();
                v.y() = c.g();
                material->ngShader()->setUniform(uniform->name, v);
                ng::Color c2 = c;
                c2.b() = 0.0f;
                c2.w() = 1.0f;
                colorVar->setValue(c2);
            }
        });
    }

    // Color3 input. Can map to a combo box if an enumeration
    else if (value->isA<mx::Color3>())
    {
        // Determine if there is an enumeration for this
        mx::Color3 color = value->asA<mx::Color3>();
        int index = -1;
        if (enumeration.size() && enumValues.size())
        {
            index = 0;
            for (size_t i = 0; i < enumValues.size(); i++)
            {
                if (enumValues[i]->asA<mx::Color3>() == color)
                {
                    index = static_cast<int>(i);
                    break;
                }
            }
        }

        // Create a combo box. The items are the enumerations in order.
        if (index >= 0)
        {
            ng::ComboBox* comboBox = new ng::ComboBox(form.window(), { "" });
            comboBox->setItems(enumeration);
            comboBox->setSelectedIndex(index);
            comboBox->setFontSize(form.widgetFontSize());
            form.addWidget(label, comboBox);
            comboBox->setCallback([this, path, enumValues, viewer](int index)
            {
                Material* material = viewer->getMaterial();
                mx::Shader::Variable* uniform = material ? material->findUniform(path) : nullptr;
                if (uniform)
                {
                    material->ngShader()->bind();
                    if (index < enumValues.size())
                    {
                        mx::Color3 c = enumValues[index]->asA<mx::Color3>();
                        ng::Vector3f v;
                        v.x() = c[0];
                        v.y() = c[1];
                        v.z() = c[2];
                        material->ngShader()->setUniform(uniform->name, v);
                    }
                }
            });
        }
        else
        {
            mx::Color3 v = value->asA<mx::Color3>();
            ng::Color c;
            c.r() = v[0];
            c.g() = v[1];
            c.b() = v[2];
            c.w() = 1.0;
            nanogui::detail::FormWidget<nanogui::Color, std::true_type>* colorVar =
                form.addVariable(label, c, true);
            colorVar->setFinalCallback([this, path, viewer](const ng::Color &c)
            {
                Material* material = viewer->getMaterial();
                mx::Shader::Variable* uniform = material ? material->findUniform(path) : nullptr;
                if (uniform)
                {
                    material->ngShader()->bind();
                    ng::Vector3f v;
                    v.x() = c.r();
                    v.y() = c.g();
                    v.z() = c.b();
                    material->ngShader()->setUniform(uniform->name, v);
                }
            });
        }
    }

    // Color4 input
    else if (value->isA<mx::Color4>())
    {
        mx::Color4 v = value->asA<mx::Color4>();
        ng::Color c;
        c.r() = v[0];
        c.g() = v[1];
        c.b() = v[2];
        c.w() = v[3];
        nanogui::detail::FormWidget<nanogui::Color, std::true_type>* colorVar =
            form.addVariable(label, c, true);
        colorVar->setFinalCallback([this, path, viewer](const ng::Color &c)
        {
            Material* material = viewer->getMaterial();
            mx::Shader::Variable* uniform = material ? material->findUniform(path) : nullptr;
            if (uniform)
            {
                material->ngShader()->bind();
                ng::Vector4f v;
                v.x() = c.r();
                v.y() = c.g();
                v.z() = c.b();
                v.w() = c.w();
                material->ngShader()->setUniform(uniform->name, v);
            }
        });
    }

    // Vec 2 widget
    else if (value->isA<mx::Vector2>())
    {
        mx::Vector2 v = value->asA<mx::Vector2>();
        nanogui::detail::FormWidget<float, std::true_type>* v1 =
            form.addVariable(label + ".x", v[0], true);
        nanogui::detail::FormWidget<float, std::true_type>* v2 =
            form.addVariable(label + ".y", v[1], true);
        v1->setCallback([this, v1, v2, path, viewer](float f)
        {
            Material* material = viewer->getMaterial();
            mx::Shader::Variable* uniform = material ? material->findUniform(path) : nullptr;
            if (uniform)
            {
                material->ngShader()->bind();
                ng::Vector2f v;
                v.x() = f;
                v.y() = v2->value();
                material->ngShader()->setUniform(uniform->name, v);
            }
        });
        v1->setSpinnable(true);
        v2->setCallback([this, v1, v2, path, viewer](float f)
        {
            Material* material = viewer->getMaterial();
            mx::Shader::Variable* uniform = material ? material->findUniform(path) : nullptr;
            if (uniform)
            {
                material->ngShader()->bind();
                ng::Vector2f v;
                v.x() = v1->value();
                v.y() = f;
                material->ngShader()->setUniform(uniform->name, v);
            }
        });
        v2->setSpinnable(true);
    }

    // Vec 3 input
    else if (value->isA<mx::Vector3>())
    {
        mx::Vector3 v = value->asA<mx::Vector3>();
        nanogui::detail::FormWidget<float, std::true_type>* v1 = 
            form.addVariable(label + ".x", v[0], true);
        nanogui::detail::FormWidget<float, std::true_type>* v2 =
            form.addVariable(label + ".y", v[1], true);
        nanogui::detail::FormWidget<float, std::true_type>* v3 =
            form.addVariable(label + ".z", v[2], true);
        v1->setCallback([this, v1, v2, v3, path, viewer](float f)
        {
            Material* material = viewer->getMaterial();
            mx::Shader::Variable* uniform = material ? material->findUniform(path) : nullptr;
            if (uniform)
            {
                material->ngShader()->bind();
                ng::Vector3f v;
                v.x() = f;
                v.y() = v2->value();
                v.z() = v3->value();
                material->ngShader()->setUniform(uniform->name, v);
            }
        });
        v1->setSpinnable(true);
        v2->setCallback([this, v1, v2, v3, path, viewer](float f)
        {
            Material* material = viewer->getMaterial();
            mx::Shader::Variable* uniform = material ? material->findUniform(path) : nullptr;
            if (uniform)
            {
                material->ngShader()->bind();
                ng::Vector3f v;
                v.x() = v1->value();
                v.y() = f;
                v.z() = v3->value();
                material->ngShader()->setUniform(uniform->name, v);
            }
        });
        v2->setSpinnable(true);
        v3->setCallback([this, v1, v2, v3, path, viewer](float f)
        {
            Material* material = viewer->getMaterial();
            mx::Shader::Variable* uniform = material ? material->findUniform(path) : nullptr;
            if (uniform)
            {
                material->ngShader()->bind();
                ng::Vector3f v;
                v.x() = v1->value();
                v.y() = v2->value();
                v.z() = f;
                material->ngShader()->setUniform(uniform->name, v);
            }
        });
        v3->setSpinnable(true);
    }

    // Vec 4 input
    else if (value->isA<mx::Vector4>())
    {
        mx::Vector4 v = value->asA<mx::Vector4>();
        nanogui::detail::FormWidget<float, std::true_type>* v1 =
            form.addVariable(label + ".x", v[0], true);
        nanogui::detail::FormWidget<float, std::true_type>* v2 =
            form.addVariable(label + ".y", v[1], true);
        nanogui::detail::FormWidget<float, std::true_type>* v3 =
            form.addVariable(label + ".z", v[2], true);
        nanogui::detail::FormWidget<float, std::true_type>* v4 =
            form.addVariable(label + ".w", v[3], true);
        v1->setCallback([this, v1, v2, v3, v4, path, viewer](float f)
        {
            Material* material = viewer->getMaterial();
            mx::Shader::Variable* uniform = material ? material->findUniform(path) : nullptr;
            if (uniform)
            {
                material->ngShader()->bind();
                ng::Vector4f v;
                v.x() = f;
                v.y() = v2->value();
                v.z() = v3->value();
                v.w() = v4->value();
                material->ngShader()->setUniform(uniform->name, v);
            }
        });
        v1->setSpinnable(true);
        v2->setCallback([this, v1, v2, v3, v4, path, viewer](float f)
        {
            Material* material = viewer->getMaterial();
            mx::Shader::Variable* uniform = material ? material->findUniform(path) : nullptr;
            if (uniform)
            {
                material->ngShader()->bind();
                ng::Vector4f v;
                v.x() = v1->value();
                v.y() = f;
                v.z() = v3->value();
                v.w() = v4->value();
                material->ngShader()->setUniform(uniform->name, v);
            }
        });
        v2->setSpinnable(true);
        v3->setCallback([this, v1, v2, v3, v4, path, viewer](float f)
        {
            Material* material = viewer->getMaterial();
            mx::Shader::Variable* uniform = material ? material->findUniform(path) : nullptr;
            if (uniform)
            {
                material->ngShader()->bind();
                ng::Vector4f v;
                v.x() = v1->value();
                v.y() = v2->value();
                v.z() = f;
                v.w() = v4->value();
                material->ngShader()->setUniform(uniform->name, v);
            }
        });
        v3->setSpinnable(true);
        v4->setCallback([this, v1, v2, v3, v4, path, viewer](float f)
        {
            Material* material = viewer->getMaterial();
            mx::Shader::Variable* uniform = material ? material->findUniform(path) : nullptr;
            if (uniform)
            {
                material->ngShader()->bind();
                ng::Vector4f v;
                v.x() = v1->value();
                v.y() = v2->value();
                v.z() = v3->value();
                v.w() = f;
                material->ngShader()->setUniform(uniform->name, v);
            }
        });
        v4->setSpinnable(true);
    }

    // String
    else if (value->isA<std::string>())
    {
        std::string v = value->asA<std::string>();
        if (!v.empty())
        {
            nanogui::detail::FormWidget<std::string, std::true_type>* stringVar =
                form.addVariable(label, v, true);
            stringVar->setCallback([this, path, viewer](const std::string &v)
            {
                Material* material = viewer->getMaterial();
                mx::Shader::Variable* uniform = material ? material->findUniform(path) : nullptr;
                if (uniform)
                {
                    if (uniform->type == MaterialX::Type::FILENAME)
                    {
                        const std::string& uniformName = uniform->name;
                        const std::string& filename = viewer->getSearchPath().find(v);
                        mx::ImageDesc desc;
                        uniform->value = mx::Value::createValue<std::string>(filename);
                        material->ngShader()->bind();
                        material->bindImage(filename, uniformName, viewer->getImageHandler(), desc);
                    }
                    else
                    {
                        uniform->value = mx::Value::createValue<std::string>(v);
                    }
                }   
            });
        }
    }
}

void PropertySheet::updateContents(Viewer* viewer)
{
    create(*viewer);

    mx::ElementPtr element = viewer->getSelectedElement();
    if (!element)
    {
        return;
    }

    Material* material = viewer->getMaterial();
    mx::DocumentPtr materialDocument = viewer->getMaterialDocument();
    if (!material || !materialDocument)
    {
        return;
    }

    GLShaderPtr shader = material->ngShader();
    mx::HwShaderPtr hwShader = material->mxShader();
    if (hwShader && shader)
    {
        const MaterialX::Shader::VariableBlock publicUniforms = hwShader->getUniformBlock(MaterialX::Shader::PIXEL_STAGE, MaterialX::Shader::PUBLIC_UNIFORMS);

        PropertySheetGroups groups;
        PropertySheetGroups unnamedGroups;
        for (auto uniform : publicUniforms.variableOrder)
        {
            if (uniform->path.size() && uniform->value)
            {
                mx::ElementPtr uniformElement = materialDocument->getDescendant(uniform->path);
                if (uniformElement && uniformElement->isA<mx::ValueElement>())
                {
                    PropertySheetItem item;
                    item.variable = uniform;
                    mx::getUIProperties(uniform->path, materialDocument, mx::EMPTY_STRING, item.ui);

                    std::string parentLabel;
                    mx::ElementPtr parent = uniformElement->getParent();
                    if (parent && parent != materialDocument && parent != element)
                    {
                        parentLabel = parent->getNamePath();
                    }
                    if (parentLabel == element->getAttribute(mx::PortElement::NODE_NAME_ATTRIBUTE))
                    {
                        parentLabel.clear();
                    }
                    if (!parentLabel.empty())
                    {
                        parentLabel += ":";
                    }

                    if (!item.ui.uiName.empty())
                    {
                        item.label = parentLabel + item.ui.uiName;
                    }
                    if (item.label.empty())
                    {
                        item.label = parentLabel + uniformElement->getName();
                    }

                    if (!item.ui.uiFolder.empty())
                    {
                        groups.insert(std::pair<std::string, PropertySheetItem>
                            (item.ui.uiFolder, item));
                    }
                    else
                    {
                        unnamedGroups.insert(std::pair<std::string, PropertySheetItem>
                            (mx::EMPTY_STRING, item));
                    }
                }
            }
        }

        std::string previousFolder;
        for (auto it = groups.begin(); it != groups.end(); ++it)
        {
            const std::string& folder = it->first;
            const PropertySheetItem& pitem = it->second;
            const mx::UIProperties& ui = pitem.ui;

            addValueToForm(pitem.variable->value, pitem.label, pitem.variable->path, ui.uiMin, ui.uiMax,
                ui.enumeration, ui.enumerationValues,
                (previousFolder == folder) ? mx::EMPTY_STRING : folder, *_form, viewer);
            previousFolder.assign(folder);
        }
        if (!groups.empty() && !unnamedGroups.empty())
        {
            _form->addGroup("Other");
        }
        for (auto it2 = unnamedGroups.begin(); it2 != unnamedGroups.end(); ++it2)
        {
            const PropertySheetItem& pitem = it2->second;
            const mx::UIProperties& ui = pitem.ui;

            addValueToForm(pitem.variable->value, pitem.label, pitem.variable->path, ui.uiMin, ui.uiMax,
                ui.enumeration, ui.enumerationValues, mx::EMPTY_STRING, *_form, viewer);
        }
    }
    viewer->performLayout();
}