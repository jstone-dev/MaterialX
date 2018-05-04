#include <MaterialXShaderGen/ShaderGenerators/Glsl/OgsFx/OgsFxSyntax.h>

#include <algorithm>

namespace MaterialX
{

OgsFxSyntax::OgsFxSyntax()
{
    //
    // Override the GLSL syntax for vector/color types there the syntax differ
    //

    addTypeSyntax
    (
        DataType::COLOR2,
        TypeSyntax
        (
            "vec2", 
            "vec2(0.0)", 
            "{0.0, 0.0}",
            "",
            "out vec2"
        )
    );

    addTypeSyntax
    (
        DataType::COLOR3,
        TypeSyntax
        (
            "vec3", 
            "vec3(0.0)", 
            "{0.0, 0.0, 0.0}",
            "",
            "out vec3"
        )
    );

    addTypeSyntax
    (
        DataType::COLOR4,
        TypeSyntax
        (
            "vec4",
            "vec4(0.0)",
            "{0.0, 0.0, 0.0, 0.0}",
            "",
            "out vec4"
        )
    );

    addTypeSyntax
    (
        DataType::VECTOR2,
        TypeSyntax
        (
            "vec2",
            "vec2(0.0)",
            "{0.0, 0.0}",
            "",
            "out vec2"
        )
    );

    addTypeSyntax
    (
        DataType::VECTOR3,
        TypeSyntax
        (
            "vec3",
            "vec3(0.0)",
            "{0.0, 0.0, 0.0}",
            "",
            "out vec3"
        )
    );

    addTypeSyntax
    (
        DataType::VECTOR4,
        TypeSyntax
        (
            "vec4",
            "vec4(0.0)",
            "{0.0, 0.0, 0.0, 0.0}",
            "",
            "out vec4"
        )
    );


    addValueConstructSyntax(
        DataType::COLOR2,
        ValueConstructSyntax(
            "vec2(", ")",
            "{", "}",
            {".r", ".g"}
        )
    );

    addValueConstructSyntax
    (
        DataType::COLOR3,
        ValueConstructSyntax
        (
            "vec3(", ")",
            "{", "}",
            {".r", ".g", ".b"}
    )
    );

    addValueConstructSyntax
    (
        DataType::COLOR4,
        ValueConstructSyntax
        (
            "vec4(", ")",
            "{", "}",
            {".r", ".g", ".b", ".a"}
    )
    );

    addValueConstructSyntax(
        DataType::VECTOR2,
        ValueConstructSyntax
        (
            "vec2(", ")",
            "{", "}",
            { ".x", ".y"}
        )
    );

    addValueConstructSyntax
    (
        DataType::VECTOR3,
        ValueConstructSyntax
        (
            "vec3(", ")",
            "{", "}",
            { ".x", ".y", ".z"}
        )
    );

    addValueConstructSyntax
    (
        DataType::VECTOR4,
        ValueConstructSyntax
        (
            "vec4(", ")",
            "{", "}",
            { ".x", ".y", ".z", ".w"}
        )
    );
}

void OgsFxSyntax::makeUnique(string& name, const string& type, UniqueNameMap& uniqueNames) const
{
    // In OGS if a color parameter is suffixed with "Color"
    // it automatically gets a color picker widgets in UI.
    // So for color parameters we renamed them accordingly 
    // to use this feature.
    if (type == DataType::COLOR3)
    {
        static const string COLOR_CAMEL = "Color";
        static const string COLOR_LOWER = "color";

        if (name.size() >= 5)
        {
            // Remove any existing "color", "Color", "COLOR" suffix
            string suffix = name.substr(name.size() - 5, string::npos);
            std::transform(suffix.begin(), suffix.end(), suffix.begin(), ::tolower);
            if (suffix == COLOR_LOWER)
            {
                const size_t n = name.size() - 5;
                name = name.substr(0, n);
                if (name.back() == '_')
                {
                    name.pop_back();
                }
            }
        }
        // Add the right suffix
        name += COLOR_CAMEL;
    }
    ParentClass::makeUnique(name, type, uniqueNames);
}

}
