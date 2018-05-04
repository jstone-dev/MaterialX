#ifndef MATERIALX_LIGHTHANDLER_H
#define MATERIALX_LIGHTHANDLER_H

#include <MaterialXCore/Definition.h>

#include <string>
#include <memory>

namespace MaterialX
{

class HwShaderGenerator;

/// Shared pointer to a LightSource
using LightSourcePtr = std::shared_ptr<class LightSource>;

/// @class @LightSource
/// Class holding data for a light source.
///
class LightSource
{
public:
    using ParameterMap = std::unordered_map<string, ValuePtr>;

    /// Return the light type
    size_t getType() const
    {
        return _typeId;
    }

    /// Return the light parameters
    const ParameterMap& getParameters() const
    {
        return _parameters;
    }

    /// Set a light parameter value
    template<typename T>
    void setParameter(const string& name, const T& value)
    {
        ParameterMap::iterator it = _parameters.find(name);
        if (it != _parameters.end())
        {
            if (!it->second)
            {
                it->second = Value::createValue<T>(value);
            }
            else
            {
                TypedValue<T>* typedVal = dynamic_cast<TypedValue<T>*>(it->second.get());
                if (!typedVal)
                {
                    throw Exception("Incorrect type when setting light paramater");
                }
                typedVal->setData(value);
            }
        }
    }

protected:
    LightSource(size_t typeId, const NodeDef& nodeDef);

    const size_t _typeId;
    ParameterMap _parameters;

    friend class LightHandler;
};


/// Shared pointer to a LightHandler
using LightHandlerPtr = std::shared_ptr<class LightHandler>;

/// @class @LightHandler
/// Utility light handler for creating and providing 
/// light data for shader binding.
///
class LightHandler
{
public:
    using LightShaderMap = std::unordered_map<size_t, ConstNodeDefPtr>;

public:
    /// Static instance create function
    static LightHandlerPtr create() { return std::make_shared<LightHandler>(); }

    /// Default constructor
    LightHandler();
    
    /// Default destructor
    virtual ~LightHandler();

    /// Add a light shader to be used for creting light sources
    void addLightShader(size_t typeId, ConstNodeDefPtr nodeDef);

    /// Create a light source of given type. The typeId must match
    /// a light shader that has been previously added to the handler.
    LightSourcePtr createLightSource(size_t typeId);

    /// Return a vector of all created light sources.
    const vector<LightSourcePtr>& getLightSources() const
    {
        return _lightSources;
    }

    /// Bind all added light shaders to the given shader generator.
    /// Only the light shaders bound to the generator will have their
    /// code emitted during shader generation.
    void bindLightShaders(HwShaderGenerator& shadergen) const;

private:
    LightShaderMap _lightShaders;
    vector<LightSourcePtr> _lightSources;
};

} // namespace MaterialX

#endif
