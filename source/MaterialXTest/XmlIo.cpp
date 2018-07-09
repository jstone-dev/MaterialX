//
// TM & (c) 2017 Lucasfilm Entertainment Company Ltd. and Lucasfilm Ltd.
// All rights reserved.  See LICENSE.txt for license.
//

#include <MaterialXTest/Catch/catch.hpp>

#include <MaterialXFormat/XmlIo.h>

namespace mx = MaterialX;

TEST_CASE("Load content", "[xmlio]")
{
    std::string libraryFilenames[] =
    {
        "stdlib_defs.mtlx",
        "stdlib_osl_impl.mtlx"
    };
    std::string exampleFilenames[] =
    {
        "CustomNode.mtlx",
        "Looks.mtlx",
        "MaterialGraphs.mtlx",
        "MultiOutput.mtlx",
        "PaintMaterials.mtlx",
        "PreShaderComposite.mtlx",
        "BxDF/alSurface.mtlx",
        "BxDF/Disney_BRDF_2012.mtlx",
        "BxDF/Disney_BSDF_2015.mtlx",
    };
    std::string searchPath = "documents/Libraries/stdlib;documents/Examples";

    // Read the standard library.
    std::vector<mx::DocumentPtr> libs;
    for (std::string filename : libraryFilenames)
    {
        mx::DocumentPtr lib = mx::createDocument();
        mx::readFromXmlFile(lib, filename, searchPath);
        REQUIRE(lib->validate());
        libs.push_back(lib);
    }

    // Read and validate each example document.
    for (std::string filename : exampleFilenames)
    {
        mx::DocumentPtr doc = mx::createDocument();
        mx::readFromXmlFile(doc, filename, searchPath);
        std::string message;
        bool docValid = doc->validate(&message);
        if (!docValid)
        {
            WARN("[" + filename + "] " + message);
        }
        REQUIRE(docValid);

        // Traverse the document tree
        int valueElementCount = 0;
        for (mx::ElementPtr elem : doc->traverseTree())
        {
            if (elem->isA<mx::ValueElement>())
            {
                valueElementCount++;
            }
        }
        REQUIRE(valueElementCount > 0);

        // Traverse the dataflow graph from each shader parameter and input
        // to its source nodes.
        for (mx::MaterialPtr material : doc->getMaterials())
        {
            REQUIRE(material->getPrimaryShaderNodeDef());
            int edgeCount = 0;
            for (mx::ParameterPtr param : material->getPrimaryShaderParameters())
            {
                REQUIRE(param->getBoundValue(material));
                for (mx::Edge edge : param->traverseGraph(material))
                {
                    edgeCount++;
                }
            }
            for (mx::InputPtr input : material->getPrimaryShaderInputs())
            {
                REQUIRE((input->getBoundValue(material) || input->getUpstreamElement(material)));
                for (mx::Edge edge : input->traverseGraph(material))
                {
                    edgeCount++;
                }
            }
            REQUIRE(edgeCount > 0);
        }

        // Serialize to XML.
        std::string xmlString = mx::writeToXmlString(doc, false);

        // Verify that the serialized document is identical.
        mx::DocumentPtr writtenDoc = mx::createDocument();
        mx::readFromXmlString(writtenDoc, xmlString);
        REQUIRE(*writtenDoc == *doc);

        // Serialize to XML with a custom predicate that skips images.
        auto skipImages = [](mx::ElementPtr elem)
        {
            return !elem->isA<mx::Node>("image");
        };
        xmlString = mx::writeToXmlString(doc, false, skipImages);
        
        // Verify that the serialized document contains no images.
        writtenDoc = mx::createDocument();
        mx::readFromXmlString(writtenDoc, xmlString);
        unsigned imageElementCount = 0;
        for (mx::ElementPtr elem : writtenDoc->traverseTree())
        {
            if (elem->isA<mx::Node>("image"))
            {
                imageElementCount++;
            }
        }
        REQUIRE(imageElementCount == 0);

        // Combine document with the standard library.
        mx::DocumentPtr doc2 = doc->copy();
        for (mx::DocumentPtr lib : libs)
        {
            doc2->importLibrary(lib);
        }
        REQUIRE(doc2->validate());

        // Verify that all referenced types and nodes are declared, and that
        // referenced node declarations are implemented.
        for (mx::ElementPtr elem : doc2->traverseTree())
        {
            mx::TypedElementPtr typedElem = elem->asA<mx::TypedElement>();
            mx::NodePtr node = elem->asA<mx::Node>();
            if (typedElem && typedElem->hasType() && !typedElem->isMultiOutputType())
            {
                REQUIRE(typedElem->getTypeDef());
            }
            if (node)
            {
                REQUIRE(node->getNodeDef());
                REQUIRE(node->getImplementation());
            }
        }

        // Create a namespaced custom library.
        mx::DocumentPtr customLibrary = mx::createDocument();
        customLibrary->setNamespace("custom");
        mx::NodeGraphPtr customNodeGraph = customLibrary->addNodeGraph("NG_custom");
        mx::NodeDefPtr customNodeDef = customLibrary->addNodeDef("ND_simpleSrf", "surfaceshader", "simpleSrf");
        mx::ImplementationPtr customImpl = customLibrary->addImplementation("IM_custom");
        mx::NodePtr customNode = customNodeGraph->addNodeInstance(customNodeDef, "custom1");
        customImpl->setNodeDef(customNodeDef);
        REQUIRE(customLibrary->validate());

        // Import the custom library.
        doc2->importLibrary(customLibrary);
        mx::NodeGraphPtr importedNodeGraph = doc2->getNodeGraph("custom:NG_custom");
        mx::NodeDefPtr importedNodeDef = doc2->getNodeDef("custom:ND_simpleSrf");
        mx::ImplementationPtr importedImpl = doc2->getImplementation("custom:IM_custom");
        mx::NodePtr importedNode = importedNodeGraph->getNode("custom1");
        REQUIRE(importedNodeDef != nullptr);
        REQUIRE(importedNode->getNodeDef() == importedNodeDef);
        REQUIRE(importedImpl->getNodeDef() == importedNodeDef);
        REQUIRE(doc2->validate());

        // Flatten subgraph references.
        for (mx::NodeGraphPtr nodeGraph : doc2->getNodeGraphs())
        {
            nodeGraph->flattenSubgraphs();
        }
        REQUIRE(doc2->validate());

        // Read document without XIncludes.
        mx::DocumentPtr doc3 = mx::createDocument();
        mx::XmlReadOptions readOptions;
        readOptions.readXIncludes = false;
        mx::readFromXmlFile(doc3, filename, searchPath, &readOptions);
        if (*doc3 != *doc)
        {
            writtenDoc = mx::createDocument();
            xmlString = mx::writeToXmlString(doc);
            mx::readFromXmlString(writtenDoc, xmlString, &readOptions);
            REQUIRE(*doc3 == *writtenDoc);
        }
    }

    // Read the same document twice with duplicate elements skipped.
    mx::DocumentPtr doc = mx::createDocument();
    mx::XmlReadOptions readOptions;
    readOptions.skipDuplicateElements = true;
    std::string filename = "PaintMaterials.mtlx";
    mx::readFromXmlFile(doc, filename, searchPath, &readOptions);
    mx::readFromXmlFile(doc, filename, searchPath, &readOptions);
    REQUIRE(doc->validate());

    // Read a non-existent document.
    mx::DocumentPtr doc2 = mx::createDocument();
    REQUIRE_THROWS_AS(mx::readFromXmlFile(doc2, "NonExistent.mtlx"), mx::ExceptionFileMissing&);
}
