# Prototype MaterialX Viewer

[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](https://github.com/materialx/MaterialX/blob/master/LICENSE.txt)
[![Travis Build Status](https://travis-ci.org/materialx/MaterialX.svg?branch=master)](https://travis-ci.org/materialx/MaterialX)
[![Appveyor Build Status](https://ci.appveyor.com/api/projects/status/pmlxnp5m1fve11k0?svg=true)](https://ci.appveyor.com/project/jstone-lucasfilm/materialx)

This repository contains the **Prototype MaterialX Viewer** (MaterialXView), which leverages ShaderX to generate GLSL shaders from MaterialX graphs, rendering the results using the NanoGUI framework.  Both the standard set of MaterialX nodes and the PBR node set in ShaderX are supported.

**Note that this prototype has been merged back to MaterialX master, and the latest version of the MaterialX viewer may be found at https://github.com/materialx/MaterialX.**

### Example Images

**Standard Surface Shader with procedural and uniform materials**
<img src="resources/Images/MaterialXView_StandardSurface_01.png" width="1024">

**Standard Surface Shader with textured, color-space-managed materials**
<img src="resources/Images/MaterialXView_StandardSurface_02.png" width="480">

### Quick Start for Developers

- Download the latest version of the [CMake](https://cmake.org/) build system.
- Point CMake to the root of the MaterialX library and generate C++ projects for your platform and compiler.
- Select the `MATERIALX_BUILD_VIEWER` option to build MaterialXView.

### Repository

The MaterialX repository consists of the following folders:

- [source](source) - Cross-platform C++ libraries for MaterialX with Python bindings.
- [python](python) - Support modules for MaterialX Python.
- [documents](documents) - MaterialX documentation, including its specification and developer guides.
- [libraries](libraries) - The standard data libraries for MaterialX, including the definitions of its pattern and shader nodes.
- [resources](resources) - Resources for rendering MaterialX content, including example materials, images, and geometry.

### Additional Resources

- The main [MaterialX repository](https://github.com/materialx/MaterialX)
- The latest [ShaderX specification](https://github.com/jstone-dev/MaterialX/blob/adsk_contrib/dev/documents/Specification/ShaderX.Draft.pdf)
- The MaterialX graph for the [Standard Surface shader](https://github.com/materialx/MaterialX/blob/master/libraries/bxdf/standard_surface.mtlx)
- An example MaterialX graph for [procedural marble](https://github.com/jstone-dev/MaterialX/blob/adsk_contrib/dev/resources/Materials/TestSuite/pbrlib/materials/standard_surface_marble_solid.mtlx)
